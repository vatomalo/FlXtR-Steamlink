"""Learn per enemy type whether guarding with R (R1 on the pad) pays off.

The proxy's `block` action holds Turbo's R guard for 15 frames. Blocking is
only worth it against enemies whose hits it actually stops, so this measures
it from live play instead of assuming:

- every decision taken with an active enemy within BLOCK_RANGE_PX is a
  sample, labelled `blocked` when the action sent was `block`;
- the sample's cost is the hero HP lost by the next observation (heals and
  KOs are ignored);
- per enemy identity, `should_block` explores until both kinds have
  MIN_SAMPLES (blocking whenever it has fewer samples than not blocking),
  then blocks only if the average loss while blocking is below
  ADVANTAGE x the average loss otherwise.

Stats persist in the AI history database (table block_stats).
"""

BLOCK_RANGE_PX = 40
MIN_SAMPLES = 8
ADVANTAGE = 0.8


def nearest_enemy(observation, limit=BLOCK_RANGE_PX):
    actor = ((observation or {}).get("tile_context") or {}).get("actor") or {}
    ax, ay = actor.get("x_px"), actor.get("y_px")
    if not isinstance(ax, int) or not isinstance(ay, int):
        return None
    best = None
    for enemy in (observation or {}).get("enemy_slots", []):
        if not (isinstance(enemy, dict) and enemy.get("active") and
                isinstance(enemy.get("x_px"), int) and isinstance(enemy.get("y_px"), int)):
            continue
        distance = abs(enemy["x_px"] - ax) + abs(enemy["y_px"] - ay)
        if distance <= limit and (best is None or distance < best[0]):
            best = (distance, enemy)
    return best[1] if best else None


class BlockLearner:
    def __init__(self, connection, log=print):
        self.db, self.log = connection, log
        self.pending = None
        with connection:
            connection.execute(
                "CREATE TABLE IF NOT EXISTS block_stats (enemy_id INTEGER NOT NULL, "
                "blocked INTEGER NOT NULL, samples INTEGER NOT NULL, hp_lost INTEGER NOT NULL, "
                "PRIMARY KEY (enemy_id, blocked))")
        self.verdicts = {}

    def stats(self, enemy_id):
        out = {False: (0, 0), True: (0, 0)}
        for blocked, samples, lost in self.db.execute(
                "SELECT blocked,samples,hp_lost FROM block_stats WHERE enemy_id=?",
                (int(enemy_id),)):
            out[bool(blocked)] = (samples, lost)
        return out

    def _add(self, enemy_id, blocked, lost):
        with self.db:
            self.db.execute(
                "INSERT INTO block_stats VALUES (?,?,1,?) ON CONFLICT(enemy_id,blocked) DO "
                "UPDATE SET samples=samples+1, hp_lost=hp_lost+excluded.hp_lost",
                (int(enemy_id), int(blocked), int(lost)))

    def proven(self, enemy_id):
        """Both sides have enough samples and blocking clearly cuts damage."""
        if not isinstance(enemy_id, int):
            return False
        (b_n, b_lost), (o_n, o_lost) = self.stats(enemy_id)[True], self.stats(enemy_id)[False]
        return (b_n >= MIN_SAMPLES and o_n >= MIN_SAMPLES and
                b_lost / b_n < ADVANTAGE * (o_lost / o_n))

    def should_block(self, enemy_id):
        if not isinstance(enemy_id, int):
            return False
        stats = self.stats(enemy_id)
        (b_n, b_lost), (o_n, o_lost) = stats[True], stats[False]
        if b_n < MIN_SAMPLES or o_n < MIN_SAMPLES:
            return b_n <= o_n
        verdict = b_lost / b_n < ADVANTAGE * (o_lost / o_n)
        if self.verdicts.get(enemy_id) != verdict:
            self.verdicts[enemy_id] = verdict
            self.log(f"[block] enemy {enemy_id:#04x}: {'blocking pays' if verdict else 'blocking does not help'} "
                     f"({b_lost / b_n:.1f} vs {o_lost / o_n:.1f} HP lost per close decision)")
        return verdict

    def record(self, observation, action):
        """Settle the previous close-range sample, then open one for `action`."""
        hero = ((observation or {}).get("party") or [{}])[0] or {}
        hp, max_hp = hero.get("hp"), hero.get("max_hp") or 0
        if self.pending is not None and isinstance(hp, int):
            enemy_id, blocked, before = self.pending
            lost = before - hp
            if hp > 0 and 0 <= lost and (not max_hp or lost < max_hp):
                self._add(enemy_id, blocked, lost)
        self.pending = None
        enemy = nearest_enemy(observation)
        if enemy is not None and isinstance(enemy.get("identity"), int) and \
                isinstance(hp, int) and hp > 0:
            self.pending = (enemy["identity"], action == "block", hp)

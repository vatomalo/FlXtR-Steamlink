"""Learn per enemy type which fighting style works, from the AI's own fights.

Until 2026-11-14 every combat rule was global: one swing distance, one guard
rule for every monster. This learner keeps, per enemy type (identity), a
running score for each tactic and makes the planner fight each monster the
way that has paid best against it:

- press: swing at full reach; guard in the recharge gap between swings
  (the style that worked on 14 Nov);
- kite:  swing at full reach; in the recharge gap step away from the enemy
  instead of guarding;
- close: only swing from point-blank range (CLOSE_SWING_PX).

Samples: every decision with an active enemy within RANGE_PX is credited to
the tactic in force for the nearest enemy's type. Its value is the damage
dealt to that enemy minus HP_WEIGHT x the hero HP lost by the next
observation (the reward ledger's weighting). Heals, KOs and map changes are
not samples. A tactic stays in force for COMMIT samples so a measurement
covers a coherent stretch of a fight; each tactic is tried until it has
MIN_SAMPLES for that enemy, then the best is used with EXPLORE exploration.
Stats persist in the AI history database (table tactic_stats).
"""
import random

TACTICS = ("press", "kite", "close")
DEFAULT = "press"
RANGE_PX = 64
CLOSE_SWING_PX = 24
HP_WEIGHT = 2.0
MIN_SAMPLES = 30
COMMIT = 20
EXPLORE = 0.1
BUILD_MARGIN = 1.0    # how much worse per decision a build's tactic may score


def _nearest(observation, limit=RANGE_PX):
    actor = ((observation or {}).get("tile_context") or {}).get("actor") or {}
    ax, ay = actor.get("x_px"), actor.get("y_px")
    if not isinstance(ax, int) or not isinstance(ay, int):
        return None
    best = None
    for enemy in (observation or {}).get("enemy_slots", []):
        if not (isinstance(enemy, dict) and enemy.get("active") and
                isinstance(enemy.get("x_px"), int) and isinstance(enemy.get("y_px"), int) and
                isinstance(enemy.get("identity"), int)):
            continue
        distance = abs(enemy["x_px"] - ax) + abs(enemy["y_px"] - ay)
        if distance <= limit and (best is None or distance < best[0]):
            best = (distance, enemy)
    return best[1] if best else None


class TacticLearner:
    def __init__(self, connection, log=print, seed=None):
        self.db, self.log = connection, log
        self.random = random.Random(seed)
        self.current = {}        # enemy_id -> [tactic, samples left]
        self.pending = None
        self.verdicts = {}
        with connection:
            connection.execute(
                "CREATE TABLE IF NOT EXISTS tactic_stats (enemy_id INTEGER NOT NULL, "
                "tactic TEXT NOT NULL, samples INTEGER NOT NULL, dealt INTEGER NOT NULL, "
                "taken INTEGER NOT NULL, PRIMARY KEY (enemy_id, tactic))")

    def stats(self, enemy_id):
        out = {tactic: (0, 0, 0) for tactic in TACTICS}
        for tactic, samples, dealt, taken in self.db.execute(
                "SELECT tactic,samples,dealt,taken FROM tactic_stats WHERE enemy_id=?",
                (int(enemy_id),)):
            if tactic in out:
                out[tactic] = (samples, dealt, taken)
        return out

    @staticmethod
    def score(samples, dealt, taken):
        return (dealt - HP_WEIGHT * taken) / samples if samples else float("-inf")

    def tactic(self, enemy_id, preferred=None, allowed=None):
        """The tactic in force against this enemy type.

        preferred: the build's tactic (som_build). It is tried first and used
        unless learned data shows it clearly worse (BUILD_MARGIN per decision)
        than the best tactic against this enemy.
        """
        if not isinstance(enemy_id, int):
            return preferred or DEFAULT
        current = self.current.get(enemy_id)
        allowed = tuple(t for t in (allowed or TACTICS) if t in TACTICS) or TACTICS
        if (current is not None and current[1] > 0 and current[2] == preferred and
                current[0] in allowed):
            return current[0]
        chosen = self._choose(enemy_id, preferred, allowed)
        self.current[enemy_id] = [chosen, COMMIT, preferred]
        return chosen

    def _choose(self, enemy_id, preferred=None, allowed=TACTICS):
        stats = self.stats(enemy_id)
        untried = [t for t in allowed if stats[t][0] < MIN_SAMPLES]
        if preferred in untried:
            return preferred
        if untried:
            return min(untried, key=lambda t: (stats[t][0], TACTICS.index(t)))
        best = max(allowed, key=lambda t: self.score(*stats[t]))
        if (preferred in allowed and preferred != best and
                self.score(*stats[preferred]) >= self.score(*stats[best]) - BUILD_MARGIN):
            best = preferred
        if self.verdicts.get(enemy_id) != best:
            self.verdicts[enemy_id] = best
            summary = ", ".join(f"{t} {self.score(*stats[t]):+.1f}" for t in allowed)
            self.log(f"[tactics] enemy {enemy_id:#04x}: {best} works best ({summary} per decision)")
        if self.random.random() < EXPLORE and len(allowed) > 1:
            return self.random.choice([t for t in allowed if t != best])
        return best

    def _add(self, enemy_id, tactic, dealt, taken):
        with self.db:
            self.db.execute(
                "INSERT INTO tactic_stats VALUES (?,?,1,?,?) ON CONFLICT(enemy_id,tactic) DO "
                "UPDATE SET samples=samples+1, dealt=dealt+excluded.dealt, "
                "taken=taken+excluded.taken",
                (int(enemy_id), tactic, int(dealt), int(taken)))

    def record(self, observation):
        """Settle the previous sample, then open one for this decision."""
        hero = ((observation or {}).get("party") or [{}])[0] or {}
        hp, max_hp = hero.get("hp"), hero.get("max_hp") or 0
        context = (observation or {}).get("tile_context") or {}
        map_id = context.get("map_id")
        if self.pending is not None and isinstance(hp, int):
            enemy_id, tactic, slot, hero_before, enemy_before, pending_map = self.pending
            lost = hero_before - hp
            if hp > 0 and 0 <= lost and (not max_hp or lost < max_hp) and map_id == pending_map:
                enemy = next((e for e in (observation or {}).get("enemy_slots", [])
                              if isinstance(e, dict) and e.get("slot") == slot), None)
                if enemy is not None and enemy.get("active") and \
                        enemy.get("identity") == enemy_id and isinstance(enemy.get("hp"), int):
                    dealt = max(0, enemy_before - enemy["hp"])
                else:
                    dealt = enemy_before          # gone: defeated (or despawned)
                self._add(enemy_id, tactic, dealt, lost)
                current = self.current.get(enemy_id)
                if current is not None and current[0] == tactic:
                    current[1] -= 1
        self.pending = None
        enemy = _nearest(observation)
        if enemy is not None and isinstance(hp, int) and hp > 0 and \
                isinstance(enemy.get("hp"), int):
            identity = enemy["identity"]
            self.pending = (identity, self.tactic(identity), enemy.get("slot"), hp,
                            enemy["hp"], map_id)

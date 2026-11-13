"""Conservative online learning of attack ranges from live WRAM outcomes.

No reinforcement of ROM code, no model training, and no unsafe game writes.
Only an unambiguous one-enemy, one-human-actor encounter is scored. The agent
must first prove what works before distance preferences affect attacks.
"""
import math
import time

BUCKET_PX = 16
OBSERVE_SECONDS = 0.8
MAX_OBSERVE_SECONDS = 1.6
MIN_SAMPLES = 4


def eligible(observation):
    context = (observation or {}).get("tile_context") or {}
    party = (observation or {}).get("party") or []
    if not context.get("runtime_verified") or context.get("game_mode") != "field" or not party:
        return None
    hero = party[0]
    if not hero.get("hp_plausible") or hero.get("hp", 0) <= 0:
        return None
    # Exclude companions, because attribution to our attack is otherwise unsafe.
    if any(p.get("availability_byte") == 1 for p in party[1:]):
        return None
    weapon = hero.get("weapon") or {}
    if not weapon.get("verified") or not weapon.get("type"):
        return None
    enemies = [e for e in (observation.get("enemy_slots") or [])
               if isinstance(e, dict) and e.get("active") and e.get("hp_plausible")]
    if len(enemies) != 1:
        return None
    enemy = enemies[0]
    actor = context.get("actor") or {}
    if not all(isinstance(actor.get(k), int) for k in ("x_px", "y_px")):
        return None
    size = context.get("map_size_tiles") or []
    if len(size) != 2 or not all(isinstance(x, int) and x > 0 for x in size):
        return None
    def wrapped(a, b, extent):
        d = abs(a - b)
        return min(d, extent - d % extent) if extent else d
    distance = (wrapped(enemy["x_px"], actor["x_px"], size[0] * 16) +
                wrapped(enemy["y_px"], actor["y_px"], size[1] * 16))
    return {"map": context.get("map_id"), "slot": enemy["slot"],
            "identity": enemy["identity"], "hp": enemy["hp"],
            "hero_hp": hero["hp"], "weapon": weapon["type"],
            "distance": distance, "bucket": min(6, distance // BUCKET_PX)}


class CombatLearner:
    def __init__(self, connection, clock=None):
        self.connection = connection
        self.clock = clock or time.monotonic
        self.pending = None
        self.connection.execute(
            "CREATE TABLE IF NOT EXISTS learned_attack_ranges "
            "(enemy_id INTEGER NOT NULL, weapon TEXT NOT NULL, range_bin INTEGER NOT NULL, "
            "attempts INTEGER NOT NULL DEFAULT 0, hits INTEGER NOT NULL DEFAULT 0, "
            "damage_done INTEGER NOT NULL DEFAULT 0, hp_lost INTEGER NOT NULL DEFAULT 0, "
            "PRIMARY KEY(enemy_id,weapon,range_bin))")
        self.connection.commit()

    def start(self, observation):
        sample = eligible(observation)
        # We only record confirmed isolated controller attacks, not charge,
        # spell, shadow clone or ambiguous multi-enemy outcomes.
        if sample is None:
            self.pending = None
            return False
        self.pending = (self.clock(), sample)
        return True

    def observe(self, observation):
        if self.pending is None:
            return None
        start, before = self.pending
        now = self.clock()
        after = eligible(observation)
        if (after is None or after["map"] != before["map"] or
                after["slot"] != before["slot"] or
                after["identity"] != before["identity"] or
                after["weapon"] != before["weapon"] or
                after["hp"] > before["hp"]):
            self.pending = None
            return None
        # Wait for delayed attack animation/damage; only count a miss after
        # a full observation window and a stable, still-alive enemy.
        damage = max(0, before["hp"] - after["hp"])
        if damage == 0 and now - start < OBSERVE_SECONDS:
            return None
        if now - start > MAX_OBSERVE_SECONDS:
            self.pending = None
            return None
        lost = max(0, before["hero_hp"] - after["hero_hp"])
        self.pending = None
        self.connection.execute(
            "INSERT INTO learned_attack_ranges "
            "(enemy_id,weapon,range_bin,attempts,hits,damage_done,hp_lost) "
            "VALUES(?,?,?,?,?,?,?) ON CONFLICT(enemy_id,weapon,range_bin) DO UPDATE SET "
            "attempts=attempts+1,hits=hits+excluded.hits,"
            "damage_done=damage_done+excluded.damage_done,hp_lost=hp_lost+excluded.hp_lost",
            (before["identity"], before["weapon"], before["bucket"],
             1, int(damage > 0), damage, lost))
        self.connection.commit()
        return {"enemy_id": before["identity"], "weapon": before["weapon"],
                "distance": before["distance"], "hit": bool(damage),
                "damage": damage, "hp_lost": lost}

    def should_close(self, identity, weapon, distance, hero_hp_fraction=1.0):
        """Only override an attack if its range is well-proven inferior.

        No intervention on unseen enemies/weapons. The occasional exploratory
        retry at a disappointing range prevents premature lock-in.
        """
        if not isinstance(distance, (int, float)) or distance < 0:
            return False
        bucket = min(6, int(distance // BUCKET_PX))
        rows = self.connection.execute(
            "SELECT range_bin,attempts,hits,damage_done,hp_lost "
            "FROM learned_attack_ranges WHERE enemy_id=? AND weapon=?",
            (identity, weapon)).fetchall()
        if not rows:
            return False
        current = next((r for r in rows if r[0] == bucket), None)
        # Never penalize an untested range. A real experiment comes first.
        if current is None or current[1] < MIN_SAMPLES:
            return False
        nearer = [r for r in rows if r[0] < bucket and r[1] >= MIN_SAMPLES]
        if not nearer:
            return False
        def score(r):
            # Bayesian-smoothed hit rate, with a mild HP cost penalty.
            return (r[2] + 1) / (r[1] + 2) - 0.08 * r[4] / max(1, r[1])
        best = max(nearer, key=score)
        if best[2] < 2 or score(best) <= score(current) + 0.2:
            return False
        attempts = sum(r[1] for r in rows)
        # One exploratory strike out of every eight cases if not at low HP.
        return hero_hp_fraction <= 0.5 or attempts % 8 != 0

    def summary(self, identity, weapon):
        rows = self.connection.execute(
            "SELECT range_bin, attempts, hits, damage_done, hp_lost FROM learned_attack_ranges "
            "WHERE enemy_id=? AND weapon=? ORDER BY range_bin",
            (identity, weapon)).fetchall()
        return [{"distance_px": [r[0]*BUCKET_PX, (r[0]+1)*BUCKET_PX-1],
                 "attempts": r[1], "hits": r[2], "damage": r[3], "hp_lost": r[4]}
                for r in rows]

"""Conservative online learning of attack ranges from live WRAM outcomes.

No reinforcement of ROM code, no model training, and no unsafe game writes.
Only isolated, controller-bound Boy encounters with an observed recovery
transition are scored. These are conservative correlations, not engine hit
events, and must not be used as discipline XP evidence.
"""
import math
import json
import time
import uuid

BUCKET_PX = 16
OBSERVE_SECONDS = 0.8
MAX_OBSERVE_SECONDS = 1.6
MIN_SAMPLES = 4
EXPLORE_EVERY = 8


def integer(value, low, high):
    return type(value) is int and low <= value <= high


def eligible(observation):
    if not isinstance(observation, dict):
        return None
    context = (observation or {}).get("tile_context") or {}
    party = (observation or {}).get("party") or []
    if (not isinstance(context, dict) or not isinstance(party, list) or len(party) != 3 or
            any(not isinstance(p, dict) for p in party) or
            not context.get("runtime_verified") or context.get("game_mode") != "field" or
            context.get("map_loading") or not observation.get("weapons_enabled")):
        return None
    hero = party[0]
    status = hero.get("status")
    if (not hero.get("hp_plausible") or not integer(hero.get("max_hp"), 1, 999) or
            not integer(hero.get("hp"), 1, hero["max_hp"]) or
            hero.get("availability_byte") != 1 or hero.get("graphic") != "boy" or
            hero.get("controller_binding") != 1 or observation.get("pad_bindings") != [1, 0, 0] or
            not isinstance(status, dict) or not status.get("verified") or status.get("raw") != 0 or
            not integer(hero.get("weapon_recharge"), 0, 255)):
        return None
    # Exclude companions, because attribution to our attack is otherwise unsafe.
    if any(p.get("availability_byte") != 0 for p in party[1:]):
        return None
    weapon = hero.get("weapon") or {}
    if (not isinstance(weapon, dict) or not weapon.get("verified") or
            weapon.get("type") not in ("glove", "sword", "axe", "spear", "whip", "bow",
                                       "boomerang", "javelin") or
            weapon.get("charge_stage") != 0):
        return None
    slots = observation.get("enemy_slots")
    if (not isinstance(slots, list) or len(slots) != 3 or
            any(not isinstance(e, dict) for e in slots) or
            sorted(e.get("slot", -1) for e in slots if integer(e.get("slot"), 1, 3)) != [1, 2, 3]):
        return None
    # Do not filter out a second occupant merely because its HP is malformed.
    enemies = [e for e in slots if e.get("active_byte") != 0]
    if len(enemies) != 1:
        return None
    enemy = enemies[0]
    if (enemy.get("active_byte") != 1 or not enemy.get("active") or
            not enemy.get("hp_plausible") or enemy.get("status") != 0 or
            not integer(enemy.get("identity"), 0, 127) or
            not integer(enemy.get("max_hp"), 1, 9999) or
            not integer(enemy.get("hp"), 1, enemy["max_hp"])):
        return None
    actor = context.get("actor") or {}
    if (not isinstance(actor, dict) or
            not all(integer(actor.get(k), 0, 65535) and integer(enemy.get(k), 0, 65535)
                    for k in ("x_px", "y_px")) or
            not integer(context.get("map_id"), 0, 511)):
        return None
    size = context.get("map_size_tiles") or []
    if not isinstance(size, list) or len(size) != 2 or not all(integer(x, 1, 128) for x in size):
        return None
    def wrapped(a, b, extent):
        d = abs(a - b) % extent
        return min(d, extent - d)
    distance = (wrapped(enemy["x_px"], actor["x_px"], size[0] * 16) +
                wrapped(enemy["y_px"], actor["y_px"], size[1] * 16))
    return {"map": context.get("map_id"), "map_size": list(size), "slot": enemy["slot"],
            "identity": enemy["identity"], "hp": enemy["hp"], "max_hp": enemy["max_hp"],
            "hero_hp": hero["hp"], "hero_max_hp": hero["max_hp"],
            "recharge": hero["weapon_recharge"], "weapon": weapon["type"],
            "distance": distance, "bucket": min(6, distance // BUCKET_PX)}


class CombatLearner:
    def __init__(self, connection, clock=None):
        self.connection = connection
        self.clock = clock or time.monotonic
        self.pending = None
        self.connection.execute("SAVEPOINT combat_learning_migration")
        try:
            self._migrate()
        except Exception:
            self.connection.execute("ROLLBACK TO combat_learning_migration")
            self.connection.execute("RELEASE combat_learning_migration")
            raise
        self.connection.execute("RELEASE combat_learning_migration")

    def _migrate(self):
        self.connection.execute(
            "CREATE TABLE IF NOT EXISTS learned_attack_ranges "
            "(enemy_id INTEGER NOT NULL, weapon TEXT NOT NULL, range_bin INTEGER NOT NULL, "
            "attempts INTEGER NOT NULL DEFAULT 0, hits INTEGER NOT NULL DEFAULT 0, "
            "damage_done INTEGER NOT NULL DEFAULT 0, hp_lost INTEGER NOT NULL DEFAULT 0, "
            "PRIMARY KEY(enemy_id,weapon,range_bin))")
        # Component versioning leaves the history database's user_version alone.
        self.connection.execute("CREATE TABLE IF NOT EXISTS combat_learning_schema "
                                "(version INTEGER PRIMARY KEY)")
        versions = self.connection.execute("SELECT version FROM combat_learning_schema").fetchall()
        if versions and versions != [(1,)]:
            raise ValueError("unsupported combat learning schema")
        self.connection.execute(
            "CREATE TABLE IF NOT EXISTS combat_learning_attempts "
            "(attempt_id TEXT PRIMARY KEY, ts REAL NOT NULL, result TEXT NOT NULL, "
            "reason TEXT NOT NULL, before_state TEXT NOT NULL, after_state TEXT, "
            "elapsed REAL NOT NULL)")
        self.connection.execute(
            "CREATE TABLE IF NOT EXISTS combat_learning_choices "
            "(enemy_id INTEGER NOT NULL, weapon TEXT NOT NULL, range_bin INTEGER NOT NULL, "
            "decisions INTEGER NOT NULL, PRIMARY KEY(enemy_id,weapon,range_bin))")
        self.connection.execute("INSERT OR IGNORE INTO combat_learning_schema VALUES(1)")

    def start(self, observation, attempt_id=None):
        self.cancel("superseded by another attack")
        sample = eligible(observation)
        # We only record confirmed isolated controller attacks, not charge,
        # spell, shadow clone or ambiguous multi-enemy outcomes.
        if sample is None or sample["recharge"] != 0:
            return False
        attempt_id = attempt_id or uuid.uuid4().hex
        with self.connection:
            inserted = self.connection.execute(
                "INSERT OR IGNORE INTO combat_learning_attempts VALUES(?,?,?,?,?,?,?)",
                (attempt_id, time.time(), "pending", "", json.dumps(sample, sort_keys=True), None, 0))
        if not inserted.rowcount:
            return False
        self.pending = {"started": self.clock(), "before": sample, "id": attempt_id,
                        "executed": False, "last": sample}
        return True

    def action_sent(self, action, observation=None):
        """Call only after a live send; menus/charge/block break attribution."""
        if action == "attack":
            return self.start(observation)
        if action not in ("wait", "step_up", "step_down", "step_left", "step_right",
                          "up", "down", "left", "right",
                          "dash_up", "dash_down", "dash_left", "dash_right"):
            self.cancel("intervening action: " + str(action))
        return False

    def cancel(self, reason="interrupted"):
        if self.pending is not None:
            with self.connection:
                self._finish("discarded", reason, self.pending["last"])

    def _finish(self, result, reason, after):
        pending = self.pending
        self.connection.execute(
            "UPDATE combat_learning_attempts SET result=?,reason=?,after_state=?,elapsed=? "
            "WHERE attempt_id=? AND result='pending'",
            (result, reason, json.dumps(after, sort_keys=True),
             max(0, self.clock() - pending["started"]), pending["id"]))
        self.pending = None

    def observe(self, observation):
        if self.pending is None:
            return None
        pending = self.pending
        start, before = pending["started"], pending["before"]
        now = self.clock()
        after = eligible(observation)
        if (after is None or after["map"] != before["map"] or
                after["map_size"] != before["map_size"] or
                after["slot"] != before["slot"] or
                after["identity"] != before["identity"] or
                after["weapon"] != before["weapon"] or
                after["max_hp"] != before["max_hp"] or
                after["hero_max_hp"] != before["hero_max_hp"] or
                after["hp"] > pending["last"]["hp"] or
                after["hero_hp"] > pending["last"]["hero_hp"]):
            self.cancel("lost isolated encounter continuity")
            return None
        if not 0 <= now - start <= MAX_OBSERVE_SECONDS:
            self.cancel("observation timeout")
            return None
        # An accepted command is not proof of a swing. The ready -> recovery
        # transition must be seen, even when an HP drop arrived earlier.
        pending["executed"] |= after["recharge"] > 0
        changed = after != pending["last"]
        pending["last"] = after
        if not pending["executed"] or not changed:
            return None
        # Wait for delayed attack animation/damage; only count a miss after
        # a full observation window and a stable, still-alive enemy.
        damage = max(0, before["hp"] - after["hp"])
        if damage == 0 and now - start < OBSERVE_SECONDS:
            return None
        lost = max(0, before["hero_hp"] - after["hero_hp"])
        with self.connection:
            self.connection.execute(
                "INSERT INTO learned_attack_ranges "
                "(enemy_id,weapon,range_bin,attempts,hits,damage_done,hp_lost) "
                "VALUES(?,?,?,?,?,?,?) ON CONFLICT(enemy_id,weapon,range_bin) DO UPDATE SET "
                "attempts=attempts+1,hits=hits+excluded.hits,"
                "damage_done=damage_done+excluded.damage_done,hp_lost=hp_lost+excluded.hp_lost",
                (before["identity"], before["weapon"], before["bucket"],
                 1, int(damage > 0), damage, lost))
            self._finish("hit" if damage else "miss", "observed ready-to-recovery transition", after)
        return {"enemy_id": before["identity"], "weapon": before["weapon"],
                "distance": before["distance"], "hit": bool(damage),
                "damage": damage, "hp_lost": lost}

    def should_close(self, identity, weapon, distance, hero_hp_fraction=1.0):
        """Only override an attack if its range is well-proven inferior.

        No intervention on unseen enemies/weapons. The occasional exploratory
        retry at a disappointing range prevents premature lock-in.
        """
        if (not isinstance(distance, (int, float)) or not math.isfinite(distance) or distance < 0 or
                not isinstance(hero_hp_fraction, (int, float)) or
                not math.isfinite(hero_hp_fraction) or not 0 < hero_hp_fraction <= 1):
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
        if hero_hp_fraction <= 0.5:
            return True
        # Count actual planning opportunities, not outcomes: closing instead
        # of attacking cannot produce an outcome to advance the old schedule.
        with self.connection:
            self.connection.execute(
                "INSERT INTO combat_learning_choices VALUES(?,?,?,1) "
                "ON CONFLICT(enemy_id,weapon,range_bin) DO UPDATE SET decisions=decisions+1",
                (identity, weapon, bucket))
            decisions = self.connection.execute(
                "SELECT decisions FROM combat_learning_choices "
                "WHERE enemy_id=? AND weapon=? AND range_bin=?",
                (identity, weapon, bucket)).fetchone()[0]
        return decisions % EXPLORE_EVERY != 0

    def summary(self, identity, weapon):
        rows = self.connection.execute(
            "SELECT range_bin, attempts, hits, damage_done, hp_lost FROM learned_attack_ranges "
            "WHERE enemy_id=? AND weapon=? ORDER BY range_bin",
            (identity, weapon)).fetchall()
        return [{"distance_px": [r[0]*BUCKET_PX, (r[0]+1)*BUCKET_PX-1],
                 "attempts": r[1], "hits": r[2], "damage": r[3], "hp_lost": r[4]}
                for r in rows]

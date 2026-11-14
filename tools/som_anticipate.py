"""Anticipate enemy attacks from what each enemy type has done before.

Monsters attack with their own reach and rhythm. Per enemy type this keeps,
for every (distance band, time since that type last hit the hero) bucket,
how often the hero lost HP by the next observation. The planner asks
`risk(...)` before a swing and guards instead when a hit is likely now.

- distance band: Manhattan distance // DIST_BAND_PX (capped);
- gap band: seconds since that enemy type last hit the hero, in GAP_EDGES
  (captures attack cadence), "never" when it has not hit yet this run;
- a sample is a decision with that type nearest within RANGE_PX; a hit is
  any HP loss by the next observation (heals, KOs, map changes ignored).
Stats persist in the AI history database (table hit_stats).
"""
import bisect
import time

RANGE_PX = 80
DIST_BAND_PX = 16
MAX_DIST_BAND = 4
# Seconds since the type's last hit, 0.25 s bands (decisions come every
# ~0.2-0.3 s); the last band is 3 s and more.
GAP_EDGES = tuple(0.25 * i for i in range(1, 13))
NEVER = 99
MIN_SAMPLES = 12


def _nearest(observation, limit=RANGE_PX):
    actor = ((observation or {}).get("tile_context") or {}).get("actor") or {}
    ax, ay = actor.get("x_px"), actor.get("y_px")
    if not isinstance(ax, int) or not isinstance(ay, int):
        return None, None
    best = None
    for enemy in (observation or {}).get("enemy_slots", []):
        if not (isinstance(enemy, dict) and enemy.get("active") and
                isinstance(enemy.get("x_px"), int) and isinstance(enemy.get("y_px"), int) and
                isinstance(enemy.get("identity"), int)):
            continue
        distance = abs(enemy["x_px"] - ax) + abs(enemy["y_px"] - ay)
        if distance <= limit and (best is None or distance < best[0]):
            best = (distance, enemy)
    return (best[1], best[0]) if best else (None, None)


class AttackPredictor:
    def __init__(self, connection, clock=time.monotonic):
        self.db, self.clock = connection, clock
        self.last_hit = {}       # enemy type -> clock time it last hit the hero
        self.pending = None
        with connection:
            connection.execute(
                "CREATE TABLE IF NOT EXISTS hit_stats (enemy_id INTEGER NOT NULL, "
                "dist_band INTEGER NOT NULL, gap_band INTEGER NOT NULL, "
                "samples INTEGER NOT NULL, hits INTEGER NOT NULL, "
                "PRIMARY KEY (enemy_id, dist_band, gap_band))")

    def bands(self, enemy_id, distance, now=None):
        now = self.clock() if now is None else now
        dist_band = min(MAX_DIST_BAND, int(distance) // DIST_BAND_PX)
        last = self.last_hit.get(enemy_id)
        gap_band = NEVER if last is None else bisect.bisect_right(GAP_EDGES, now - last)
        return dist_band, gap_band

    def risk(self, enemy_id, distance, now=None):
        """(probability of a hit by the next decision, samples) for this moment."""
        if not isinstance(enemy_id, int) or not isinstance(distance, (int, float)):
            return 0.0, 0
        dist_band, gap_band = self.bands(enemy_id, distance, now)
        row = self.db.execute(
            "SELECT samples,hits FROM hit_stats WHERE enemy_id=? AND dist_band=? AND gap_band=?",
            (enemy_id, dist_band, gap_band)).fetchone()
        if not row:
            return 0.0, 0
        samples, hits = row
        return (hits + 1) / (samples + 2), samples

    def record(self, observation):
        """Settle the previous decision's sample, then open one for this one."""
        now = self.clock()
        hero = ((observation or {}).get("party") or [{}])[0] or {}
        hp, max_hp = hero.get("hp"), hero.get("max_hp") or 0
        map_id = ((observation or {}).get("tile_context") or {}).get("map_id")
        if self.pending is not None and isinstance(hp, int):
            enemy_id, dist_band, gap_band, before, pending_map = self.pending
            lost = before - hp
            if hp > 0 and 0 <= lost and (not max_hp or lost < max_hp) and map_id == pending_map:
                hit = lost > 0
                with self.db:
                    self.db.execute(
                        "INSERT INTO hit_stats VALUES (?,?,?,1,?) ON CONFLICT(enemy_id,dist_band,"
                        "gap_band) DO UPDATE SET samples=samples+1, hits=hits+excluded.hits",
                        (enemy_id, dist_band, gap_band, int(hit)))
                if hit:
                    self.last_hit[enemy_id] = now
        self.pending = None
        enemy, distance = _nearest(observation)
        if enemy is not None and isinstance(hp, int) and hp > 0:
            dist_band, gap_band = self.bands(enemy["identity"], distance, now)
            self.pending = (enemy["identity"], dist_band, gap_band, hp, map_id)

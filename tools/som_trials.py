"""Shared trial learner: tunes the AI's hand-set numbers from its own fights.

A knob is a decision threshold with a few candidate values (KNOBS), e.g.
how close an enemy must be before Kawarimi is armed. Per enemy type the
learner tries each value, scores it by damage dealt minus HP_WEIGHT x HP
lost per decision next to that enemy, and then keeps the best one, still
re-trying another now and then (EXPLORE). An enemy type it knows little
about borrows the all-enemy record, so new monsters start from experience
instead of from scratch.

Usage each decision (the bridge):
    value = trials.value("kawarimi_px", enemy_identity)   # use it now
    trials.record(observation)                            # settle + sample

Values are committed for COMMIT decisions per enemy type, so one fight is
judged under one setting rather than a mix. Pure Python + sqlite.
"""
import random
import time

from som_tactics import _nearest

HP_WEIGHT = 2.0
MIN_SAMPLES = 40          # decisions per value before its score is trusted
COMMIT = 25               # decisions a chosen value is kept per enemy type
EXPLORE = 0.1             # chance to re-try another value once all are known
MARGIN = 0.5              # the default value stays unless clearly worse
STALE_S = 2.0             # a sample not settled by then is dropped
ALL = -1                  # context id of the all-enemy record

# knob: (default, candidates). Defaults are the old hand-set numbers.
KNOBS = {
    # Kawarimi: arm when an enemy is this close (px) ...
    "kawarimi_px": (40, (24, 40, 64)),
    # ... and HP is below this fraction (or a guard is wanted); 1.01 = always.
    "kawarimi_hp": (0.5, (0.5, 0.75, 1.01)),
    # Shunshin: cast the clones when an enemy is this close (px).
    "shunshin_px": (80, (48, 80, 120)),
}


def score(samples, total):
    return total / samples if samples else float("-inf")


class TrialLearner:
    def __init__(self, connection, log=print, seed=None, knobs=None, clock=None):
        self.db, self.log = connection, log
        self.clock = clock or time.monotonic
        self.knobs = dict(knobs or KNOBS)
        self.random = random.Random(seed)
        self.current = {}        # (knob, context) -> [value, decisions left]
        self.in_use = {}         # knob -> (context, value) used this decision
        self.pending = None
        self.verdicts = {}
        with connection:
            connection.execute(
                "CREATE TABLE IF NOT EXISTS trial_stats (knob TEXT NOT NULL, "
                "context INTEGER NOT NULL, value REAL NOT NULL, samples INTEGER NOT NULL, "
                "total REAL NOT NULL, PRIMARY KEY (knob, context, value))")

    def stats(self, knob, context):
        out = {value: (0, 0.0) for value in self.knobs[knob][1]}
        for value, samples, total in self.db.execute(
                "SELECT value,samples,total FROM trial_stats WHERE knob=? AND context=?",
                (knob, int(context))):
            for candidate in out:
                if abs(candidate - value) < 1e-9:
                    out[candidate] = (samples, total)
        return out

    def value(self, knob, context=None):
        """The value of knob in force against this enemy type (None = any)."""
        default, candidates = self.knobs[knob]
        context = ALL if not isinstance(context, int) else context
        key = (knob, context)
        current = self.current.get(key)
        if current is None or current[1] <= 0:
            current = self.current[key] = [self._choose(knob, context), COMMIT]
        self.in_use[knob] = (context, current[0])
        return current[0]

    def _choose(self, knob, context):
        default, candidates = self.knobs[knob]
        stats = self.stats(knob, context)
        if context != ALL and all(stats[v][0] < MIN_SAMPLES for v in candidates):
            stats = self.stats(knob, ALL)          # new enemy: borrow experience
        untried = [v for v in candidates if stats[v][0] < MIN_SAMPLES]
        if default in untried:
            return default
        if untried:
            return min(untried, key=lambda v: (stats[v][0], candidates.index(v)))
        best = max(candidates, key=lambda v: score(*stats[v]))
        if best != default and score(*stats[default]) >= score(*stats[best]) - MARGIN:
            best = default
        if self.verdicts.get(key := (knob, context)) != best:
            self.verdicts[key] = best
            summary = ", ".join(f"{v:g} {score(*stats[v]):+.1f}" for v in candidates)
            where = "all enemies" if context == ALL else f"enemy {context:#04x}"
            self.log(f"[trials] {knob} vs {where}: {best:g} works best ({summary})")
        if self.random.random() < EXPLORE:
            return self.random.choice([v for v in candidates if v != best])
        return best

    def record(self, observation):
        """Settle the previous decision's sample, then open one for this one."""
        hero = ((observation or {}).get("party") or [{}])[0] or {}
        hp, max_hp = hero.get("hp"), hero.get("max_hp") or 0
        map_id = ((observation or {}).get("tile_context") or {}).get("map_id")
        if (self.pending is not None and isinstance(hp, int) and
                self.clock() - self.pending[-1] <= STALE_S):
            used, slot, identity, hero_before, enemy_before, pending_map, _ = self.pending
            lost = hero_before - hp
            if hp > 0 and 0 <= lost and (not max_hp or lost < max_hp) and map_id == pending_map:
                enemy = next((e for e in (observation or {}).get("enemy_slots", [])
                              if isinstance(e, dict) and e.get("slot") == slot), None)
                if (enemy is not None and enemy.get("active") and
                        enemy.get("identity") == identity and isinstance(enemy.get("hp"), int)):
                    dealt = max(0, enemy_before - enemy["hp"])
                else:
                    dealt = enemy_before          # gone: defeated (or despawned)
                self._add(used, dealt - HP_WEIGHT * lost)
        self.pending = None
        used, self.in_use = self.in_use, {}
        enemy = _nearest(observation)
        if (used and enemy is not None and isinstance(hp, int) and hp > 0 and
                isinstance(enemy.get("hp"), int)):
            self.pending = (used, enemy.get("slot"), enemy.get("identity"), hp,
                            enemy["hp"], map_id, self.clock())

    def _add(self, used, reward):
        with self.db:
            for knob, (context, value) in used.items():
                for where in {context, ALL}:
                    self.db.execute(
                        "INSERT INTO trial_stats VALUES(?,?,?,1,?) ON CONFLICT(knob,context,value) "
                        "DO UPDATE SET samples=samples+1,total=total+excluded.total",
                        (knob, where, value, reward))
                current = self.current.get((knob, context))
                if current is not None and current[0] == value:
                    current[1] -= 1

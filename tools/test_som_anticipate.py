import sqlite3
import unittest

from som_anticipate import NEVER, AttackPredictor
from som_planner import MemoryPlanner
from test_som_split import state


def obs(hp, distance=20, identity=7, map_id=129):
    return {"party": [{"hp": hp, "max_hp": 100}],
            "enemy_slots": [{"active": True, "slot": 4, "identity": identity,
                             "x_px": 40 + distance, "y_px": 56}],
            "tile_context": {"map_id": map_id, "actor": {"x_px": 40, "y_px": 56}}}


class AttackPredictorTest(unittest.TestCase):
    def setUp(self):
        self.db = sqlite3.connect(":memory:")
        self.addCleanup(self.db.close)
        self.now = [0.0]
        self.p = AttackPredictor(self.db, clock=lambda: self.now[0])

    def test_learns_the_attack_rhythm_of_an_enemy_type(self):
        # Enemy 7 hits every 1.2 s at 20 px; decisions every 0.3 s.
        hp = 100
        for step in range(200):
            self.now[0] = step * 0.3
            hit = step % 4 == 3
            hp = hp - 1 if hit else hp
            if hp < 20:
                hp = 100
                self.p.pending = None
            self.p.record(obs(hp))
        # A decision 0.9 s after a hit is followed by the next hit (due at
        # 1.2 s); 0.3-0.6 s after a hit it is not.
        self.assertGreater(self.p.risk(7, 20, now=self.p.last_hit[7] + 0.9)[0], 0.8)
        self.assertLess(self.p.risk(7, 20, now=self.p.last_hit[7] + 0.6)[0], 0.1)
        self.assertEqual(self.p.risk(8, 20), (0.0, 0))          # unknown type

    def test_bands_without_a_previous_hit(self):
        self.assertEqual(self.p.bands(7, 20)[1], NEVER)


class Risky:
    def __init__(self, risk):
        self.value = risk

    def risk(self, enemy_id, distance, now=None):
        return self.value, 50


class PlannerAnticipationTest(unittest.TestCase):
    def test_guards_before_a_likely_hit_but_never_more_than_twice(self):
        planner = MemoryPlanner()
        planner.attack_predictor = Risky(0.8)
        ticks = iter(range(100))
        planner.clock = lambda: float(next(ticks))
        observed = state([(16, 0)])
        actions = [planner.decide(observed, previous=p).action
                   for p in ("step_right", "step_right", "block", "attack")]
        self.assertEqual(actions, ["block", "block", "attack", "block"])   # then counter

    def test_swings_when_a_hit_is_unlikely(self):
        planner = MemoryPlanner()
        planner.attack_predictor = Risky(0.1)
        ticks = iter(range(100))
        planner.clock = lambda: float(next(ticks))
        observed = state([(16, 0)])
        planner.decide(observed, previous="step_right")
        self.assertEqual(planner.decide(observed, previous="step_right").action, "attack")


if __name__ == "__main__":
    unittest.main()

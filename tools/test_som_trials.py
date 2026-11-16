"""Shared trial learner: thresholds tuned from damage dealt vs HP lost."""
import sqlite3
import unittest

from som_trials import ALL, COMMIT, MIN_SAMPLES, TrialLearner


class Clock:
    def __init__(self):
        self.now = 0.0

    def __call__(self):
        return self.now


def state(hero_hp, enemy_hp, identity=7):
    return {"party": [{"hp": hero_hp, "max_hp": 100}],
            "tile_context": {"map_id": 1, "actor": {"x_px": 0, "y_px": 0}},
            "enemy_slots": [{"slot": 1, "active": True, "identity": identity,
                             "x_px": 20, "y_px": 0, "hp": enemy_hp}]}


class TrialLearnerTest(unittest.TestCase):
    def setUp(self):
        self.clock = Clock()
        self.knobs = {"px": (40, (24, 40, 64))}
        self.trials = TrialLearner(sqlite3.connect(":memory:"), log=lambda line: None,
                                   seed=1, knobs=self.knobs, clock=self.clock)

    def fight(self, rewards, decisions, identity=7):
        """Play decisions; each value's damage dealt per decision from rewards."""
        hero, enemy = 90, 1000
        for _ in range(decisions):
            value = self.trials.value("px", identity)
            self.trials.record(state(hero, enemy, identity))
            enemy -= rewards[value]
            self.clock.now += 0.5
        return value

    def test_default_first_then_every_value_then_the_best(self):
        self.assertEqual(self.trials.value("px", 7), 40)
        self.fight({24: 1, 40: 2, 64: 6}, 3 * MIN_SAMPLES + 2 * COMMIT)
        self.assertTrue(all(n >= MIN_SAMPLES for n, _ in self.trials.stats("px", 7).values()))
        self.trials.random.random = lambda: 1.0              # no exploring
        self.trials.current.clear()
        self.assertEqual(self.trials.value("px", 7), 64)

    def test_a_new_enemy_borrows_the_all_enemy_record(self):
        self.fight({24: 1, 40: 2, 64: 6}, 3 * MIN_SAMPLES + 2 * COMMIT)
        self.assertGreater(self.trials.stats("px", ALL)[64][0], 0)
        self.trials.random.random = lambda: 1.0
        self.assertEqual(self.trials.value("px", 99), 64)

    def test_default_kept_unless_clearly_worse(self):
        self.fight({24: 2, 40: 2, 64: 1}, 3 * MIN_SAMPLES + 2 * COMMIT)
        self.trials.random.random = lambda: 1.0
        self.trials.current.clear()
        self.assertEqual(self.trials.value("px", 7), 40)

    def test_rare_knobs_judge_sooner(self):
        trials = TrialLearner(sqlite3.connect(":memory:"), log=lambda line: None, seed=1,
                              knobs={"evade": (0, (0, 1), 10)}, clock=self.clock)
        seen = []
        hero = 90
        for _ in range(25):
            seen.append(trials.value("evade", 7))
            trials.record(state(hero, 100))
            self.clock.now += 0.5
        self.assertIn(1, seen[:21])                          # evade tried within 20

    def test_stale_samples_are_dropped(self):
        self.trials.value("px", 7)
        self.trials.record(state(90, 100))
        self.clock.now += 10
        self.trials.record(state(50, 100))                   # 40 HP lost, but long after
        self.assertEqual(self.trials.stats("px", 7)[40], (0, 0.0))


if __name__ == "__main__":
    unittest.main()

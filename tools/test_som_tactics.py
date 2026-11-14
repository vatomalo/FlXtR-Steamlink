import sqlite3
import unittest
from unittest import mock

import som_tactics as tactics
from som_planner import MemoryPlanner
from test_som_split import state


def obs(hero_hp, enemy_hp=50, identity=7, enemy_px=(56, 56), slot=4, map_id=129, max_hp=100):
    enemies = [] if enemy_px is None else [
        {"active": True, "slot": slot, "x_px": enemy_px[0], "y_px": enemy_px[1],
         "identity": identity, "hp": enemy_hp}]
    return {"party": [{"hp": hero_hp, "max_hp": max_hp}], "enemy_slots": enemies,
            "tile_context": {"map_id": map_id, "actor": {"x_px": 40, "y_px": 56}}}


class TacticLearnerTest(unittest.TestCase):
    def setUp(self):
        self.db = sqlite3.connect(":memory:")
        self.addCleanup(self.db.close)
        self.logs = []
        self.learner = tactics.TacticLearner(self.db, self.logs.append, seed=1)

    def test_samples_credit_damage_both_ways(self):
        self.learner.record(obs(100, enemy_hp=50))
        self.learner.record(obs(96, enemy_hp=41))        # dealt 9, took 4
        self.assertEqual(self.learner.stats(7)["press"], (1, 9, 4))

    def test_a_defeated_enemy_counts_its_remaining_hp(self):
        self.learner.record(obs(100, enemy_hp=12))
        self.learner.record(obs(100, enemy_px=None))
        self.assertEqual(self.learner.stats(7)["press"], (1, 12, 0))

    def test_heals_kos_and_map_changes_are_not_samples(self):
        for before, after in ((obs(50), obs(90)),                          # heal
                              (obs(50), obs(0)),                           # KO
                              (obs(50), obs(50, map_id=130))):             # map change
            self.learner.pending = None
            self.learner.record(before)
            self.learner.record(after)
        self.assertEqual(self.learner.stats(7)["press"][0], 0)

    def test_explores_each_tactic_then_keeps_the_best(self):
        with mock.patch.object(tactics, "EXPLORE", 0.0):
            order, hp = [], 100
            for _ in range(400):
                if all(n >= tactics.MIN_SAMPLES for n, _, _ in self.learner.stats(7).values()):
                    break
                tactic = self.learner.tactic(7)
                if not order or order[-1] != tactic:
                    order.append(tactic)
                lost = {"press": 2, "kite": 0, "close": 3}[tactic]
                self.learner.record(obs(hp, enemy_hp=50))
                hp -= lost
                self.learner.record(obs(hp, enemy_hp=45))
                self.learner.pending = None
                hp = 100
            self.assertEqual(set(order), set(tactics.TACTICS))
            for _ in range(tactics.COMMIT + 1):
                self.learner.current.clear()
                self.assertEqual(self.learner.tactic(7), "kite")
        self.assertTrue(any("kite works best" in line for line in self.logs))

    def test_a_tactic_is_kept_for_a_stretch_of_fighting(self):
        first = self.learner.tactic(7)
        for _ in range(tactics.COMMIT - 1):
            self.learner.record(obs(100))
            self.assertEqual(self.learner.tactic(7), first)


class BuildPreferenceTest(unittest.TestCase):
    def test_build_tactic_goes_first_and_stays_unless_clearly_worse(self):
        db = sqlite3.connect(":memory:")
        learner = tactics.TacticLearner(db, lambda _: None, seed=1)
        self.assertEqual(learner.tactic(7, "kite"), "kite")             # tried first
        def fill(tactic, dealt, taken):
            db.execute("INSERT OR REPLACE INTO tactic_stats VALUES (7,?,?,?,?)",
                       (tactic, tactics.MIN_SAMPLES, dealt, taken))
        with mock.patch.object(tactics, "EXPLORE", 0.0):
            fill("press", 60, 0); fill("kite", 50, 0); fill("close", 0, 0)   # 2.0 vs 1.67
            learner.current.clear()
            self.assertEqual(learner.tactic(7, "kite"), "kite")          # within margin
            fill("kite", 0, 15)                                           # -1.0: clearly worse
            learner.current.clear()
            self.assertEqual(learner.tactic(7, "kite"), "press")
            learner.current.clear()
            self.assertEqual(learner.tactic(7, None), "press")           # balanced: best


class FixedTactic:
    def __init__(self, name):
        self.name = name

    def tactic(self, enemy_id, preferred=None):
        return self.name


class PlannerTacticsTest(unittest.TestCase):
    def paced(self, name):
        planner = MemoryPlanner()
        planner.tactic_learner = FixedTactic(name)
        now = [0.0]
        planner.clock = lambda: now[0]
        return planner, now

    def test_kite_steps_away_in_the_recharge_gap(self):
        planner, now = self.paced("kite")
        observed = state([(16, 0)])
        planner.decide(observed, previous="step_right")
        now[0] = 1.0
        self.assertEqual(planner.decide(observed, previous="step_right").action, "attack")
        now[0] = 1.3
        gap = planner.decide(observed, previous="attack")
        self.assertEqual((gap.action, gap.intention), ("step_left", "kite_enemy"))

    def test_press_guards_in_the_recharge_gap(self):
        planner, now = self.paced("press")
        observed = state([(16, 0)])
        planner.decide(observed, previous="step_right")
        now[0] = 1.0
        planner.decide(observed, previous="step_right")
        now[0] = 1.3
        self.assertEqual(planner.decide(observed, previous="attack").action, "block")

    def test_close_only_swings_from_point_blank(self):
        planner, now = self.paced("close")
        planner.decide(state([(28, 0)]), previous="step_right")
        now[0] = 5.0
        self.assertNotEqual(planner.decide(state([(28, 0)]), previous="step_right").action,
                            "attack")
        now[0] = 9.0
        self.assertEqual(planner.decide(state([(16, 0)]), previous="step_right").action,
                         "attack")


if __name__ == "__main__":
    unittest.main()

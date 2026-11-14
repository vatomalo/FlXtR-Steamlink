import unittest

from som_build import classify, profile
from som_planner import MemoryPlanner
from test_som_split import state


def stats(s, a, c, i, w):
    return {"strength": s, "agility": a, "constitution": c, "intelligence": i, "wisdom": w}


class BuildTest(unittest.TestCase):
    def test_live_characters(self):
        self.assertEqual(classify(stats(19, 19, 18, 8, 9)), "balanced")    # boy, level 5
        self.assertEqual(classify(stats(11, 11, 11, 7, 15)), "caster")     # girl template
        self.assertEqual(classify(stats(8, 13, 9, 15, 5)), "balanced")     # sprite: 15 vs 13

    def test_clear_leads(self):
        self.assertEqual(classify(stats(25, 19, 18, 8, 9)), "brawler")
        self.assertEqual(classify(stats(18, 24, 17, 8, 9)), "skirmisher")
        self.assertEqual(classify(stats(18, 17, 24, 8, 9)), "guardian")
        self.assertEqual(classify(None), "balanced")
        self.assertEqual(classify(stats(0, 0, 0, 0, 0)), "balanced")


class Risky:
    def risk(self, enemy_id, distance, now=None):
        return 0.45, 50


class BuildPlannerTest(unittest.TestCase):
    def planner(self, build):
        planner = MemoryPlanner()
        planner.build, planner.build_profile = build, profile(build)
        planner.attack_predictor = Risky()
        ticks = iter(range(100))
        planner.clock = lambda: float(next(ticks))
        return planner

    def test_reaction_to_a_predicted_hit_depends_on_the_build(self):
        observed = state([(16, 0)])
        brawler = self.planner("brawler")                    # 45 % < 55 %: swings
        brawler.decide(observed, previous="step_right")
        self.assertEqual(brawler.decide(observed, previous="step_right").action, "attack")
        guardian = self.planner("guardian")                  # guards
        self.assertEqual(guardian.decide(observed, previous="step_right").action, "block")
        skirmisher = self.planner("skirmisher")              # sidesteps
        dodge = skirmisher.decide(observed, previous="step_right")
        self.assertEqual((dodge.action, dodge.intention), ("step_left", "kite_enemy"))


if __name__ == "__main__":
    unittest.main()

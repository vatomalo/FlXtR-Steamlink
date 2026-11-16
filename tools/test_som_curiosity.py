"""Curiosity: rewards for new places, penalties for lingering (no emulator)."""
import unittest

from som_curiosity import (AREA_TILES, LINGER_DECISIONS, LINGER_PENALTY, NEW_AREA_REWARD,
                           NEW_MAP_REWARD, Curiosity)
from som_planner import MemoryPlanner
from test_som_planner import observation


class CuriosityTests(unittest.TestCase):
    def test_new_map_and_area_paid_once(self):
        c = Curiosity()
        kinds = [k for k, _, _ in c.observe(130, 1, (3, 3))]
        self.assertEqual(kinds, ["new_map", "new_area"])
        self.assertEqual(c.score, NEW_MAP_REWARD + NEW_AREA_REWARD)
        self.assertEqual(c.observe(130, 1, (4, 3)), [])            # same area
        self.assertEqual([k for k, _, _ in c.observe(130, 1, (3 + AREA_TILES, 3))], ["new_area"])
        self.assertEqual(c.observe(130, 1, (3, 3)), [])             # back again: nothing
        self.assertTrue(c.area_seen(130, 1, (0, 0)))
        self.assertFalse(c.area_seen(131, 1, (0, 0)))

    def test_lingering_is_penalised_once_per_stretch(self):
        c = Curiosity()
        events = []
        for i in range(LINGER_DECISIONS):
            events += c.observe(130, 1, (5 + i % 2, 5))
        self.assertIn("linger", [k for k, _, _ in events])
        score = c.score
        for i in range(LINGER_DECISIONS - 1):                          # cooldown
            self.assertEqual(c.observe(130, 1, (5, 5)), [])
        self.assertEqual(c.score, score)
        self.assertEqual(c.lingers, 1)

    def test_busy_or_moving_is_not_lingering(self):
        c = Curiosity()
        for i in range(LINGER_DECISIONS * 2):
            c.observe(130, 1, (5, 5), busy=(i % 10 == 0))         # a fight now and then
        for i in range(LINGER_DECISIONS * 2):
            c.observe(130, 1, (i % 30, 5))                          # walking around
        self.assertEqual(c.lingers, 0)

    def test_state_round_trip(self):
        c = Curiosity()
        c.observe(130, 1, (3, 3))
        d = Curiosity(c.to_state())
        self.assertEqual(d.score, c.score)
        self.assertEqual(d.observe(130, 1, (3, 3)), [])
        self.assertEqual(Curiosity({"bad": 1}).score, 0)

    def test_escape_heading_turns_around(self):
        self.assertEqual(Curiosity.escape_heading("explore_east"), "explore_west")
        self.assertEqual(Curiosity.escape_heading("explore_unknown"), "explore_unknown")


class PlannerCuriosityTests(unittest.TestCase):
    def test_planner_reports_reward_and_reacts_to_lingering(self):
        planner = MemoryPlanner()
        first = planner.decide(observation())
        self.assertIn("curiosity new_map +%d" % NEW_MAP_REWARD, first.reason)
        reasons = [planner.decide(observation(), directive="explore_east").reason
                   for _ in range(LINGER_DECISIONS)]
        self.assertTrue(any("linger -%d" % LINGER_PENALTY in r for r in reasons))
        self.assertEqual(planner.escape_directive, "explore_west")
        self.assertGreater(planner.escape_left, 0)
        self.assertIn((131, 1, (2, 3)), planner.frontier_rejected)

    def test_fighting_in_place_is_not_penalised(self):
        planner = MemoryPlanner()
        enemy = {"slot": 1, "active": True, "x_px": 64, "y_px": 56}
        for _ in range(LINGER_DECISIONS + 5):
            planner.decide(observation(enemy=enemy))
        self.assertEqual(planner.curiosity.lingers, 0)


class ReinforcementTests(unittest.TestCase):
    def test_discovery_credits_the_way_there_and_dead_ends_are_blamed(self):
        c = Curiosity()
        c.observe(5, 1, (0, 0))                       # new map: area 0,0
        c.observe(5, 1, (AREA_TILES, 0))              # walked east
        c.observe(5, 1, (2 * AREA_TILES, 0))          # new area found
        east, start = c.value(5, 1, (AREA_TILES, 0)), c.value(5, 1, (0, 0))
        self.assertGreater(east, 0)
        self.assertGreater(start, 0)
        # The newest step back earns most of each later discovery.
        self.assertGreater(c.value(5, 1, (2 * AREA_TILES, 0)), 0)
        before = c.value(5, 1, (0, 3 * AREA_TILES))
        c.trail.clear()
        kind, points, _ = c.punish(5, 1, (0, 3 * AREA_TILES))
        self.assertEqual(kind, "dead_end")
        self.assertLess(points, 0)
        self.assertLess(c.value(5, 1, (0, 3 * AREA_TILES)), before)
        # Values survive a restart.
        again = Curiosity(c.to_state())
        self.assertAlmostEqual(again.value(5, 1, (0, 3 * AREA_TILES)),
                               c.value(5, 1, (0, 3 * AREA_TILES)), places=1)

    def test_lingering_lowers_the_area(self):
        c = Curiosity()
        for _ in range(LINGER_DECISIONS):
            c.observe(5, 1, (3, 3))
        self.assertLess(c.value(5, 1, (3, 3)), 50 + NEW_AREA_REWARD)

    def test_planner_makes_dead_end_areas_cost_more(self):
        planner = MemoryPlanner()
        planner.curiosity = Curiosity()
        self.assertIsNone(planner._dead_end_weights(131, 1, {(1, 1): "open"}))
        planner.curiosity.values["131:1:0:0"] = -30.0
        weights = planner._dead_end_weights(131, 1, {(1, 1): "open", (9, 1): "open"})
        self.assertGreater(weights[(1, 1)], 1)
        self.assertNotIn((9, 1), weights)


if __name__ == "__main__":
    unittest.main()

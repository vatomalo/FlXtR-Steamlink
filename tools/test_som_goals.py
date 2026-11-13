"""Qwen goal chooser tests: allowlist, parsing, planning; no model needed."""
import tempfile
import unittest
from pathlib import Path

from som_goals import (build_goal_prompt, goal_plan, is_travel, memory_summary,
                       offered_goals, parse_goal)
from som_memory import ObservationHistory


def summary(**overrides):
    base = {"map_id": 129, "hp": 60, "max_hp": 100, "level": 5, "gold": 639,
            "candy": 1, "enemies_near": 0, "inn_map": 131, "shop_map": 257,
            "shop_ready": True, "known_maps": [128, 129, 130, 131],
            "reward_maps": [{"map_id": 128, "score": 90, "gold": 120, "hp_lost": 15},
                            {"map_id": 129, "score": 40, "gold": 60, "hp_lost": 10},
                            {"map_id": 130, "score": -30, "gold": 0, "hp_lost": 15}],
            "avoided_enemies": []}
    base.update(overrides)
    return base


class TestGoals(unittest.TestCase):
    def test_only_achievable_goals_are_offered(self):
        goals = offered_goals(summary())
        self.assertEqual(goals, ["hunt_here", "go_to_inn", "go_to_shop",
                                 "return_to_potos", "hunt_map_128"])
        # Full HP: no inn; no shop errand; in Potos; enemies near; free heading.
        goals = offered_goals(summary(hp=100, shop_ready=False, map_id=131,
                                      enemies_near=2), hunt_direction="none")
        self.assertEqual(goals[:3], ["hunt_here", "engage_enemy", "avoid_enemy"])
        self.assertIn("explore_west", goals)
        self.assertFalse({"go_to_inn", "go_to_shop", "return_to_potos"} & set(goals))
        # Maps without a known route are never offered; losing maps neither.
        goals = offered_goals(summary(), reachable=lambda m: m == 131)
        self.assertNotIn("hunt_map_128", goals)
        self.assertNotIn("hunt_map_130", offered_goals(summary(map_id=128)))

    def test_parses_small_model_replies_but_only_offered_goals(self):
        offered = ["hunt_here", "go_to_inn", "hunt_map_128"]
        self.assertEqual(parse_goal("<think></think>Go to inn.", offered), "go_to_inn")
        self.assertEqual(parse_goal("hunt map 128", offered), "hunt_map_128")
        self.assertIsNone(parse_goal("hunt_map_130", offered))
        self.assertIsNone(parse_goal("press start", offered))

    def test_goals_become_headings_or_travel_errands(self):
        facts = summary()
        directive, errand, done = goal_plan("hunt_map_128", facts)
        self.assertEqual((directive, errand["anchor"], done),
                         ("explore_west", {"map_id": 128}, False))
        self.assertTrue(goal_plan("hunt_map_129", facts)[2])          # already there
        inn = {"map_id": 131, "layer": 1, "target": [5, 5]}
        self.assertEqual(goal_plan("go_to_inn", facts, inn_anchor=inn)[1]["arrive"],
                         "recover_at_inn")
        self.assertEqual(goal_plan("avoid_enemy", facts)[0], "avoid_enemy")
        self.assertTrue(is_travel("return_to_potos"))
        self.assertFalse(is_travel("hunt_here"))

    def test_prompt_is_short_and_lists_the_allowlist(self):
        prompt = build_goal_prompt("gain levels", summary(), ["hunt_here", "go_to_inn"],
                                   "hunt_here")
        self.assertIn("Goals: hunt_here, go_to_inn", prompt)
        self.assertIn("map 128 score 90", prompt)
        self.assertLess(len(prompt), 900)

    def test_summary_comes_from_memory(self):
        with tempfile.TemporaryDirectory() as directory:
            history = ObservationHistory(Path(directory) / "goals.sqlite")
            history.set_agent_state("safe_anchor", {"map_id": 131, "layer": 1,
                                                    "target": [3, 3]})
            history.connection.execute(
                "INSERT INTO reward_ledger VALUES(128,0,5,40,30.0,3,0)")
            facts = memory_summary(history, {"party": [{"hp": 50, "max_hp": 100}],
                                             "tile_context": {"map_id": 129}}, 639)
            self.assertEqual((facts["inn_map"], facts["gold"], facts["reward_maps"][0]["map_id"]),
                             (131, 639, 128))
            history.close()


if __name__ == "__main__":
    unittest.main()

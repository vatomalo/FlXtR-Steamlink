import sqlite3
import unittest
from unittest.mock import patch

from som_planner import MemoryPlanner
from som_story import MANTIS_ID, MANTIS_MAP, POTOS_MAP, ROAD_MAP, story_stage
from som_tactics import MANTIS_TACTICS, MIN_SAMPLES, TacticLearner
from test_som_split import state


class Flags:
    def __init__(self, story, pit):
        self.values = {0x7ECF10: story, 0x7ECF0B: pit}

    def u8(self, address):
        return self.values[address]


def arena(hp=300, hero_hp=80):
    snapshot = state([(0, 0)])
    snapshot["tile_context"].update(map_id=MANTIS_MAP, game_mode="field", runtime_verified=True)
    snapshot["party"][0].update(hp=hero_hp, max_hp=100)
    snapshot["enemy_slots"][0].update(identity=MANTIS_ID, slot=1, hp=hp,
                                      max_hp=300, hp_plausible=True, active=hp > 0, x_px=56, y_px=40)
    return snapshot


class PotosStoryTest(unittest.TestCase):
    def test_checkpoints_require_expected_map_flags_and_verified_field(self):
        obs = arena()
        self.assertEqual(story_stage(Flags(6, 1), obs), "fight")
        self.assertIsNone(story_stage(Flags(6, 0), obs))
        self.assertIsNone(story_stage(Flags(6, 1), arena(hp=0)))
        context = obs["tile_context"]
        context["map_id"] = POTOS_MAP
        self.assertIsNone(story_stage(Flags(7, 2), obs))
        self.assertEqual(story_stage(Flags(9, 2), obs), "leave")
        context["map_id"] = ROAD_MAP
        self.assertEqual(story_stage(Flags(9, 2), obs), "road_reached")
        context["game_mode"] = "event"
        self.assertIsNone(story_stage(Flags(9, 2), obs))

    def test_circle_uses_open_step_and_only_once_per_swing(self):
        planner = MemoryPlanner()
        planner.mantis_lesson = True
        snapshot = arena()
        context = snapshot["tile_context"]
        context["actor"].update(x_px=36, y_px=40)
        planner.clock = lambda: 10
        planner.last_swing = 9.8
        first = planner._combat(snapshot, context, {"step_up", "step_right"})
        self.assertEqual((first.action, first.intention), ("step_up", "kite_enemy"))
        self.assertEqual(planner.mantis_trial, (MANTIS_ID, 1, "circle"))
        second = planner._combat(snapshot, context, {"step_up", "step_right"})
        self.assertNotEqual(second.reason, first.reason)

    def test_only_attributable_boss_damage_is_recorded_and_changes_preference(self):
        with sqlite3.connect(":memory:") as db:
            learner = TacticLearner(db, log=lambda _: None)
            def trial(tactic, after):
                learner.record_trial(arena(), (MANTIS_ID, 1, tactic))
                learner.settle(after)
            missing = arena()
            missing["enemy_slots"] = []
            trial("circle", missing)
            bad = arena(hp=250)
            bad["tile_context"]["runtime_verified"] = False
            trial("circle", bad)
            self.assertEqual(learner.stats(MANTIS_ID)["circle"], (0, 0, 0))
            for _ in range(MIN_SAMPLES):
                trial("circle", arena(hp=285, hero_hp=70))
                trial("press", arena(hp=287, hero_hp=80))
            self.assertEqual(learner.stats(MANTIS_ID)["circle"],
                             (MIN_SAMPLES, 15 * MIN_SAMPLES, 10 * MIN_SAMPLES))
            with patch("som_tactics.EXPLORE", 0):
                self.assertEqual(learner.tactic(MANTIS_ID, "circle", MANTIS_TACTICS), "press")

import copy
import json
import unittest

from som_npc_schedule import DEFAULT_PATH, ScheduleError, World
from som_world_clock import parse_time

BASE = json.loads(DEFAULT_PATH.read_text())


class ScheduleTest(unittest.TestCase):
    def setUp(self):
        self.world = World(copy.deepcopy(BASE))
        self.npc = self.world.npcs["potos_villager_c7"]

    def test_entry_at_wraps_to_previous_day(self):
        self.assertEqual(self.npc.entry_at(parse_time("03:00")).activity, "sleep")
        self.assertEqual(self.npc.entry_at(parse_time("09:30")).activity, "work")
        self.assertEqual(self.npc.entry_at(parse_time("12:00")).activity, "eat")

    def test_boundaries_span_days(self):
        moments = self.npc.boundaries(parse_time("21:30"), 1440 + parse_time("07:00"))
        self.assertEqual([entry.activity for _, entry in moments],
                         ["sleep", "wake_up", "eat"])

    def test_aliases_and_abstract_home_proxy(self):
        home = self.world.location(self.npc.resolve_name("home"))
        self.assertTrue(home.abstract)
        self.assertEqual(self.world.on_map(home, 131).tile, (27, 25))
        self.assertIsNone(self.world.on_map(self.world.location("potos_lane_west"), 257))
        self.assertIsNone(self.npc.resolve_name("stay"))

    def test_story_flags_gate_existence(self):
        self.assertTrue(self.npc.story_allows({0x10: 3}))
        self.assertFalse(self.npc.story_allows({0x10: 7}))
        self.assertTrue(self.npc.story_allows({}))

    def test_validation(self):
        for mutate in (
                lambda d: d["npcs"][0]["schedule"].append(
                    {"time": "06:00", "activity": "work", "destination": "home"}),
                lambda d: d["npcs"][0]["schedule"].append(
                    {"time": "05:00", "activity": "dance", "destination": "home"}),
                lambda d: d["npcs"][0]["schedule"].append(
                    {"time": "05:00", "activity": "work", "destination": "nowhere"}),
                lambda d: d["locations"]["potos_lane_west"].pop("evidence"),
                lambda d: d["locations"]["potos_c7_house"].pop("proxy"),
                lambda d: d["npcs"][0]["native"].pop("record"),
                lambda d: d.update(version=2),
                lambda d: d["npcs"].append(copy.deepcopy(d["npcs"][0]))):
            data = copy.deepcopy(BASE)
            mutate(data)
            with self.assertRaises((ScheduleError, ValueError)):
                World(data)

    def test_potos_binding_is_the_verified_static_record(self):
        self.assertEqual(self.npc.native["map"], 131)
        self.assertEqual(self.npc.native["record"], 8)
        self.assertEqual(self.npc.native["kind"], 0xC7)
        self.assertTrue(self.npc.native["static"])


if __name__ == "__main__":
    unittest.main()

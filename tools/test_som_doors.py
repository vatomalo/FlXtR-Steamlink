"""Native return doors, observed lessons and disconnected shared-map rooms."""
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from som_doors import DoorLearner
from som_memory import ObservationHistory, WRAM
from som_planner import MemoryPlanner


def room(map_id=257, x=2, y=2, marked=True):
    data = bytearray(0x20000)
    def word(offset, value):
        data[offset:offset+2] = value.to_bytes(2, "little")
    data[0xE000], data[0xE00B], data[0xE180] = 1, 0x80, 0x80
    data[0xE4], data[0xE6] = 1, 0x0F
    word(0xDC, map_id)
    word(0xC0, 8*16)
    word(0xC2, 6*16)
    word(0xE002, x*16+8)
    word(0xE004, y*16+8)
    word(0xE182, 80)
    word(0xE184, 100)
    for ty in range(6):
        for tx in range(8):
            # Two rooms separated by walls; neither wraps across the map edge.
            if tx in (0, 4, 7) or ty in (0, 5):
                data[0x14000+ty*128+tx] = 1
    data[0x1BC04] = 3
    data[0x14000+4*128+2] = 0x7F
    if marked:
        data[0x1BC00+0x7F*4+1] = 0x10
    return WRAM(data)


class TestDoors(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.history = ObservationHistory(Path(self.tmp.name)/"history.sqlite")
        self.doors = DoorLearner(self.history)

    def tearDown(self):
        self.history.close()
        self.tmp.cleanup()

    def observe(self, wram):
        observation = wram.observe()
        self.doors.observe(wram, observation)
        self.history.remember_world({"tile_context": wram.tile_context(radius=4)})
        return observation

    def test_native_marker_is_a_candidate_not_a_confirmed_crossing(self):
        observation = self.observe(room())
        self.assertEqual(observation["tile_context"]["return_doors"][0]["target"], [2, 4])
        place = self.history.place_summary()[0]
        self.assertEqual(place["kind"], "return_door")
        self.assertEqual(place["source"], "attribute_b1_bit4")

    def test_tile_id_alone_does_not_make_a_door(self):
        self.assertEqual(self.observe(room(marked=False))["tile_context"]["return_doors"], [])
        self.assertEqual(self.history.place_summary(), [])

    def test_loading_and_unverified_observations_do_not_teach_doors(self):
        observation = room().observe()
        observation["tile_context"].update(runtime_verified=False, map_loading=True)
        self.doors.observe(room(), observation)
        self.assertNotIn("return_doors", observation["tile_context"])
        self.assertEqual(self.history.place_summary(), [])

    def test_actual_crossing_confirms_the_nearby_door_and_survives_restart(self):
        self.observe(room(y=3))
        self.observe(room(map_id=131, marked=False))
        place = self.history.place_summary(257)[0]
        self.assertEqual(place["name"], "Return door to map 131")
        self.assertEqual(place["source"], "observed_map_transition")
        self.doors = DoorLearner(self.history)
        self.observe(room())
        self.assertEqual(self.history.place_summary(257)[0]["source"], "observed_map_transition")

    def test_a_distant_map_change_does_not_confirm_the_door(self):
        self.observe(room(x=6, y=1))
        self.observe(room(map_id=131, marked=False))
        self.assertEqual(self.history.place_summary(257)[0]["source"], "attribute_b1_bit4")

    def test_unreachable_shop_in_same_map_routes_to_the_actual_exit(self):
        observation = self.observe(room())
        planner = MemoryPlanner(self.history)
        context = observation["tile_context"]
        decision = planner._travel(context, {"map_id": 257, "layer": 2, "target": [6, 2]},
                                   ["step_up", "step_down", "step_left", "step_right"],
                                   "shop", "go_to_shop", "at_shop")
        self.assertEqual((decision.action, decision.intention), ("step_down", "leave_room"))
        self.assertIn("disconnected room", decision.reason)

    def test_entrance_selection_uses_arrival_room_not_map_id_alone(self):
        self.observe(room())
        planner = MemoryPlanner(self.history)
        wrong = {"next_map": 257, "arrival": [2, 2], "arrival_layer": 2}
        right = {"next_map": 257, "arrival": [6, 3], "arrival_layer": 2}
        old = {"next_map": 257}
        self.assertEqual(planner._doors_toward_room([wrong, old, right],
            {"map_id": 257, "layer": 2, "target": [6, 2]}), [right])

    def test_exiting_requires_a_verified_route_and_open_step(self):
        observation = self.observe(room())
        planner = MemoryPlanner(self.history)
        self.assertEqual(planner._return_door(observation["tile_context"], []).action, "wait")

    def test_rejected_room_entrance_cannot_fall_back_to_an_old_map_edge(self):
        observation = self.observe(room())
        context = dict(observation["tile_context"], map_id=131)
        planner = MemoryPlanner(self.history)
        wrong = {"next_map": 257, "arrival": [2, 2], "arrival_layer": 2}
        with patch.object(self.history, "exit_waypoints", return_value=[wrong]):
            decision = planner._travel(context,
                {"map_id": 257, "layer": 2, "target": [6, 2]}, [],
                "shop", "go_to_shop", "at_shop")
        self.assertEqual((decision.action, decision.intention), ("wait", "observe"))
        self.assertIn("no doorway", decision.reason)


if __name__ == "__main__":
    unittest.main()

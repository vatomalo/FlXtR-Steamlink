import copy
import json
import sqlite3
import unittest

import som_living_world as lw
import som_npc_native as native
from som_memory import WRAM
from som_npc_schedule import DEFAULT_PATH, World
from som_world_clock import WorldClock, WorldTime, parse_time
from test_som_npc_native import synthetic

NPC = "potos_villager_c7"


def at(text, day=0):
    return WorldTime(day * 1440 + parse_time(text))


class LivingWorldTest(unittest.TestCase):
    def setUp(self):
        self.db = sqlite3.connect(":memory:")
        self.world = lw.LivingWorld(self.db, World(json.loads(DEFAULT_PATH.read_text())),
                                    WorldClock(self.db, real=0.0))

    def test_first_sight_places_npc_at_scheduled_place(self):
        state = self.world.advance(NPC, at("10:00"))
        self.assertEqual((state["activity"], state["location"]), ("work", "potos_lane_west"))

    def test_off_screen_day_without_frames(self):
        self.world.advance(NPC, at("10:00"))
        evening = self.world.advance(NPC, at("19:00"))
        self.assertEqual((evening["activity"], evening["location"]),
                         ("socialize", "potos_lane_east"))
        night = self.world.advance(NPC, at("23:00"))
        self.assertEqual((night["activity"], night["location"]), ("sleep", "potos_c7_house"))
        morning = self.world.advance(NPC, at("09:30", day=1))
        self.assertEqual((morning["activity"], morning["location"]), ("work", "potos_lane_west"))

    def test_travel_takes_estimated_time(self):
        self.world.advance(NPC, at("07:30"))           # at home (abstract house)
        state = self.world.advance(NPC, at("08:00"))
        self.assertIsNotNone(state["travel"])
        self.assertEqual(state["location"], "potos_c7_house")
        arrive = state["travel"]["arrive"]
        self.assertGreater(arrive, at("08:00").minutes)
        done = self.world.advance(NPC, WorldTime(arrive))
        self.assertIsNone(done["travel"])
        self.assertEqual(done["location"], "potos_lane_west")

    def test_state_persists_across_instances(self):
        self.world.advance(NPC, at("12:30"))
        again = lw.LivingWorld(self.db, self.world.world, self.world.clock)
        self.assertEqual(again.load_state(NPC)["activity"], "eat")

    def test_interruption_delays_departure_and_is_remembered(self):
        self.world.advance(NPC, at("07:58"))
        self.world.interrupt(NPC, at("07:59"), minutes=10)
        state = self.world.advance(NPC, at("08:00"))
        self.assertEqual(state["travel"]["depart"], at("08:09").minutes)
        self.assertEqual(self.world.encounters(NPC), 1)

    def test_story_override_removes_npc(self):
        state = self.world.advance(NPC, at("10:00"), flags={0x10: 7})
        self.assertEqual(state["override"], "story_absent")
        self.assertIsNone(self.world.desired_place(NPC, state, 131))
        state = self.world.advance(NPC, at("10:05"), flags={0x10: 3})
        self.assertIsNone(state["override"])

    def test_abstract_home_maps_to_doorstep(self):
        state = self.world.advance(NPC, at("23:00"))
        self.assertEqual(self.world.desired_place(NPC, state, 131).name, "potos_c7_doorstep")
        self.assertIsNone(self.world.desired_place(NPC, state, 130))

    def test_director_override_is_validated(self):
        self.world.override_day(NPC, 0, parse_time("18:00"), "potos_c7_house")
        state = self.world.advance(NPC, at("18:30"))
        self.assertEqual(state["destination"], "potos_c7_house")
        with self.assertRaises(ValueError):
            self.world.override_day(NPC, 0, parse_time("18:30"), "potos_c7_house")
        with self.assertRaises(ValueError):
            self.world.override_day(NPC, 0, parse_time("18:00"), "potos_c7_spawn")

    def test_context_lines(self):
        night = self.world.advance(NPC, at("23:00"))
        self.assertIn("Zzz", self.world.context_line(NPC, night, at("23:00")))
        evening = self.world.advance(NPC, at("21:10", day=1))
        self.assertIn("Monsters", self.world.context_line(NPC, evening, at("21:10", day=1)))
        for minute in ("10:00", "10:10"):
            self.world.interrupt(NPC, at(minute, day=2))
        work = self.world.advance(NPC, at("10:20", day=2))
        self.assertIn("again", self.world.context_line(NPC, work, at("10:20", day=2)))
        self.assertFalse(lw.MESSAGE_ALLOWED.search(
            self.world.context_line(NPC, work, at("10:20", day=2))))


class DriverTest(unittest.TestCase):
    def setUp(self):
        self.world = World(json.loads(DEFAULT_PATH.read_text()))
        self.place = self.world.location("potos_lane_west")

    def test_plans_native_walk_toward_place(self):
        driver = lw.NativeDriver(self.world, NPC)
        command, reason = driver.plan(WRAM(bytes(synthetic())), self.place, 131)
        self.assertTrue(command.startswith("npc_walk 131 8 199 472 404 "), reason)

    def test_never_drives_wandering_or_unloaded_or_other_map(self):
        driver = lw.NativeDriver(self.world, NPC)
        self.assertIsNone(driver.plan(WRAM(bytes(synthetic(static=False))), self.place, 131)[0])
        self.assertIsNone(driver.plan(WRAM(bytes(synthetic(record=3))), self.place, 131)[0])
        self.assertIsNone(driver.plan(WRAM(bytes(synthetic())), self.place, 130)[0])

    def test_waits_during_events_and_yields_to_player(self):
        data = synthetic()
        data[0xD0] = 0x81
        driver = lw.NativeDriver(self.world, NPC)
        self.assertEqual(driver.plan(WRAM(bytes(data)), self.place, 131),
                         (None, "event running"))
        near = synthetic(hero_tile=(28, 26))
        command, reason = lw.NativeDriver(self.world, NPC).plan(
            WRAM(bytes(near)), self.world.location("potos_c7_doorstep"), 131)
        self.assertIsNone(command)
        self.assertEqual(reason, "yielding to the player")

    def test_arrived_and_blocked_steps_replan_then_give_up(self):
        doorstep = self.world.location("potos_c7_doorstep")
        arrived = synthetic(npc_tile=(27, 25))
        self.assertEqual(lw.NativeDriver(self.world, NPC).plan(WRAM(bytes(arrived)), doorstep,
                                                              131), (None, "at place"))
        driver = lw.NativeDriver(self.world, NPC)
        wram = WRAM(bytes(synthetic()))
        sent = []
        for _ in range(10):
            command, _ = driver.plan(wram, self.place, 131)   # NPC never moves
            if command:
                sent.append(command)
        self.assertGreaterEqual(len(sent), 2)
        self.assertLessEqual(len(sent), lw.MAX_BLOCKS + 1)
        self.assertGreater(driver.pause, 0)

    def test_talk_target_detection(self):
        data = synthetic()
        self.assertIsNone(lw.talk_target(WRAM(bytes(data)), None, self.world))
        data[0xD0] = 0x81
        data[0x3A], data[0x3B] = 0x00, 0xF0
        self.assertEqual(lw.talk_target(WRAM(bytes(data)), None, self.world), NPC)

    def test_remote_loop_only_accepts_numeric_walks(self):
        self.assertIn("*[!0-9\\ ]*", lw.REMOTE_LOOP)
        self.assertNotIn("flxtr-som-ai-action", lw.REMOTE_LOOP)

    def test_observe_once_reports_without_sending(self):
        world = lw.LivingWorld(sqlite3.connect(":memory:"), self.world,
                               WorldClock(sqlite3.connect(":memory:"), real=0.0))
        world.clock.set_time(parse_time("10:00"), 0.0)
        now, lines, commands = lw.observe_once(world, WRAM(bytes(synthetic())), real=0.0)
        self.assertEqual(now.minute_of_day, parse_time("10:00"))
        self.assertIn("slot 8", lines[0])
        self.assertEqual(len(commands), 1)


if __name__ == "__main__":
    unittest.main()

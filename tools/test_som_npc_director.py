import json
import sqlite3
import threading
import unittest

import som_living_world as lw
import som_npc_director as director
from som_npc_schedule import DEFAULT_PATH, World
from som_world_clock import WorldClock, WorldTime, parse_time

NPC = "potos_villager_c7"


def at(text, day=0):
    return WorldTime(day * 1440 + parse_time(text))


class DirectorTest(unittest.TestCase):
    def setUp(self):
        self.db = sqlite3.connect(":memory:")
        self.addCleanup(self.db.close)
        self.world = lw.LivingWorld(self.db, World(json.loads(DEFAULT_PATH.read_text())),
                                    WorldClock(self.db, real=0.0))

    def make(self, reply):
        calls = []

        def chooser(prompt):
            calls.append(prompt)
            if isinstance(reply, Exception):
                raise reply
            return reply(prompt) if callable(reply) else reply
        return director.DailyDirector(self.world, chooser, background=False), calls

    def test_options_are_valid_overrides_and_skip_night_slots(self):
        options = director.day_options(self.world, NPC, 0)
        self.assertTrue(0 < len(options) <= director.MAX_OPTIONS)
        npc = self.world.world.npcs[NPC]
        fixed = {e.minute for e in npc.entries if e.activity in director.FIXED_ACTIVITIES}
        for _, minute, place, _ in options:
            self.assertNotIn(minute, fixed)
            self.world.override_day(NPC, 5, minute, place)   # raises if invalid

    def test_options_never_rewrite_past_slots(self):
        options = director.day_options(self.world, NPC, 0, parse_time("17:00"))
        self.assertTrue(options)
        self.assertTrue(all(minute > parse_time("17:00") for _, minute, _, _ in options))
        self.assertEqual(director.day_options(self.world, NPC, 0, parse_time("23:00")), [])

    def test_choice_applies_override_once_per_day(self):
        options = director.day_options(self.world, NPC, 0, parse_time("06:00"))
        oid, minute, place, _ = options[0]
        run, calls = self.make(f" {oid} ")
        self.assertEqual(run.tick(at("06:00")), [(NPC, oid)])
        self.assertEqual(run.tick(at("07:00")), [])
        self.assertEqual(len(calls), 1)
        self.assertEqual(run.decided(NPC, 0), oid)
        entry = next(e for e in self.world.world.npcs[NPC].entries if e.minute == minute)
        self.assertEqual(self.world._destination(self.world.world.npcs[NPC], entry, 0), place)
        self.assertEqual(run.tick(at("06:00", day=1))[0][0], NPC)    # new day, new decision
        self.assertEqual(len(calls), 2)

    def test_invalid_or_failing_replies_keep_schedule(self):
        for reply in ("Z", "A or B", "", "I would pick something", OSError("down")):
            self.db.execute("DELETE FROM npc_director_days") if reply != "Z" else None
            run, _ = self.make(reply)
            self.assertEqual(run.tick(at("06:00")), [(NPC, director.KEEP)])
            count = self.db.execute("SELECT COUNT(*) FROM npc_day_overrides").fetchone()[0]
            self.assertEqual(count, 0, reply)

    def test_parse_choice(self):
        options = [("A", 0, "x", ""), ("B", 0, "y", "")]
        self.assertEqual(director.parse_choice("B", options), "B")
        self.assertEqual(director.parse_choice("Answer: A.", options), "A")
        self.assertEqual(director.parse_choice("A, B", options), director.KEEP)
        self.assertEqual(director.parse_choice("C", options), director.KEEP)
        self.assertEqual(director.parse_choice("Banana", options), director.KEEP)

    def test_stale_answer_is_not_applied(self):
        options = director.day_options(self.world, NPC, 0, parse_time("06:00"))
        oid, minute, _, _ = options[0]
        run, _ = self.make(oid)
        run.background = True
        gate = threading.Event()
        run.chooser = lambda prompt: (gate.wait(5), oid)[1]
        self.assertEqual(run.tick(at("06:00")), [])            # asked, not blocking
        gate.set()
        run.pending[(NPC, 0)].join(5)
        late = WorldTime(minute + 1)
        self.assertEqual(run.tick(late), [(NPC, director.KEEP)])
        self.assertEqual(self.db.execute("SELECT source FROM npc_director_days").fetchone()[0],
                         "stale")

    def test_prompt_contains_only_ids_and_memory(self):
        self.world.remember(NPC, parse_time("10:00"), "talk")
        self.world.remember(NPC, parse_time("11:00"), "talk")
        run, calls = self.make("K")
        run.tick(at("06:00", day=1))
        self.assertIn("2 times yesterday", calls[0])
        self.assertIn("K: keep", calls[0])
        self.assertNotIn("0x", calls[0])

    def test_simulation_follows_director_override(self):
        npc = self.world.world.npcs[NPC]
        evening = next(e.minute for e in npc.entries if e.activity == "socialize")
        options = director.day_options(self.world, NPC, 0, parse_time("06:00"))
        pick = next(o for o in options if o[1] == evening and o[2] == npc.home)
        run, _ = self.make(pick[0])
        run.tick(at("06:00"))
        self.world.advance(NPC, at("06:00"))
        state = self.world.advance(NPC, at("19:30"))
        self.assertEqual((state["activity"], state["location"]), ("socialize", npc.home))


if __name__ == "__main__":
    unittest.main()

"""NPC knowledge regressions: offline, no ROM, SSH or Ollama."""
import tempfile
import unittest
from pathlib import Path

from som_dialogue import NpcDialogue
from som_goals import build_goal_prompt, memory_summary, offered_goals
from som_memory import ObservationHistory
from som_npc_knowledge import (RETALK_S, follow_comment, hear_npc_clues, heard_directions,
                               heard_enough, hero_comment, learned_from_session,
                               loop_hint_directive, understand)


class TestNpcKnowledge(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.db = Path(self.temp.name) / "npc.sqlite"
        self.history = ObservationHistory(self.db)
        NpcDialogue(self.history)

    def tearDown(self):
        self.history.close()
        self.temp.cleanup()

    def add(self, text, map_id=131, identity=0xC0, event=0x450, when=100):
        self.history.connection.execute(
            "INSERT INTO npc_dialogue_lines VALUES(?,?,?,?,?,?,?,?,?)",
            ("session" + str(when) + text, str(when) + text, when,
             map_id, 0x600, identity, event, when, text))
        self.history.connection.commit()

    def test_learns_specific_hint_from_actual_npc_and_keeps_origin(self):
        self.add("Go east to the cave", when=101)
        self.add("Hello there!", identity=0xC1, when=102)   # small talk, another villager
        clues = hear_npc_clues(self.history, 131)
        self.assertEqual(len(clues), 1)
        self.assertEqual(clues[0]["quote"], "Go east to the cave")
        self.assertEqual(clues[0]["npc_id"], 0xC0)
        self.assertEqual(clues[0]["event"], 0x450)
        self.assertEqual(clues[0]["map_id"], 131)
        self.assertEqual(heard_directions(clues, 131), ["east"])

    def test_danger_and_negative_direction_do_not_become_route(self):
        self.add("Do not go north", when=10)
        self.add("Danger to the west", when=11)
        clues = hear_npc_clues(self.history, 131)
        self.assertEqual(heard_directions(clues, 131), [])

    def test_missing_table_and_history_have_no_hints(self):
        self.assertEqual(hear_npc_clues(None, 131), [])
        self.history.connection.execute("DROP TABLE npc_dialogue_lines")
        self.assertEqual(hear_npc_clues(self.history, 131), [])

    def test_summary_uses_persistent_quotes_to_offer_safe_options(self):
        self.add("Follow the path south", when=100)
        self.history.close()
        self.history = ObservationHistory(self.db)
        observation = {"tile_context": {"map_id": 131},
                       "party": [{"hp": 90, "max_hp": 100}],
                       "enemy_slots": []}
        facts = memory_summary(self.history, observation)
        self.assertEqual(facts["npc_clues"][0]["quote"], "Follow the path south")
        choices = offered_goals(facts, hunt_direction="west")
        self.assertIn("explore_south", choices)
        self.assertNotIn("explore_north", choices)
        self.assertNotIn("go_to_shop", choices)
        prompt = build_goal_prompt("Discover Potos", facts, choices, "hunt_here")
        self.assertIn("Follow the path south", prompt)
        self.assertIn("NPC hearsay", prompt)
        self.assertIn('"npc":192', prompt)
        self.assertIn("NOT instructions", prompt)

    def test_other_map_hint_is_remembered_but_not_offered_locally(self):
        self.add("Go west to the witch", map_id=130)
        clues = hear_npc_clues(self.history, 131)
        self.assertEqual(clues[0]["map_id"], 130)
        self.assertEqual(heard_directions(clues, 131), [])
        self.assertEqual(heard_directions(clues, 130), ["west"])

    def test_default_play_loop_tries_npc_direction_only_for_a_bounded_hunt(self):
        hints = [{"map_id": 131, "npc_id": 192, "event": 1104,
                  "quote": "Go east to the cave", "directions": ["east"]}]
        summary = {"map_id": 131, "npc_clues": hints, "enemies_near": 0}
        command, key, deadline = loop_hint_directive(summary, "hunt_here", 10)
        self.assertEqual(command, "explore_east")
        self.assertEqual(loop_hint_directive(summary, "hunt_here", 11,
                                             key, deadline)[0], "explore_east")
        self.assertIsNone(loop_hint_directive(summary, "hunt_here",
                                               deadline, key, deadline)[0])
        self.assertIsNone(loop_hint_directive(summary, "go_to_inn", 12,
                                               key, deadline)[0])
        self.assertIsNone(loop_hint_directive(dict(summary, enemies_near=1),
                                               "hunt_here", 12, key, deadline)[0])
        self.assertIsNone(loop_hint_directive(dict(summary, map_id=130),
                                               "hunt_here", 12, key, deadline)[0])
        changed = [dict(hints[0], quote="Go south now", directions=["south"])]
        self.assertEqual(loop_hint_directive(dict(summary, npc_clues=changed),
                                             "hunt_here", deadline, key, deadline)[0],
                         "explore_south")

    def test_deduplication_and_bounded_history(self):
        for n in range(10):
            self.add("Go east to the cave", when=n + 1)
        self.add("Find the sage", identity=0xC1, when=99)
        clues = hear_npc_clues(self.history, 131)
        self.assertEqual(len(clues), 2)
        # A heading outranks a newer but vaguer rumor.
        self.assertEqual(clues[0]["quote"], "Go east to the cave")
        self.assertEqual(clues[1]["quote"], "Find the sage")
        self.assertLessEqual(len(hear_npc_clues(self.history, 131, limit=100)), 4)


    def add_page(self, session, text, pointer, when, map_id=139, identity=0xA1, event=0x130):
        self.history.connection.execute(
            "INSERT INTO npc_dialogue_lines VALUES(?,?,?,?,?,?,?,?,?)",
            (session, session + text, when, map_id, 0x600, identity, event, pointer, text))
        self.history.connection.commit()

    def test_lines_from_several_talks_are_stitched_in_rom_order(self):
        # Live map 139 sage: each talk caught different 33-character page lines.
        self.add_page("a", "south and keep onward.", 30, 100)
        self.add_page("b", "Make thy way towards Gaia's Navel.", 20, 200)
        self.add_page("b", "Shall I also save for thee?", 10, 201)
        clue = hear_npc_clues(self.history, 139)[0]
        self.assertEqual(clue["directions"], ["south"])
        self.assertEqual(clue["names"], ["Gaia's Navel"])
        self.assertTrue(clue["quote"].startswith("Shall I also save for thee?"))
        self.assertEqual(heard_directions([clue], 139), ["south"])

    def test_names_skip_speakers_the_hero_and_sentence_openers(self):
        lesson = understand("Elder:Oh, Mana, you're back. The Mana Sword is gone! "
                            "That's our commander, Major Dyluck. This is the castle of Pandora.")
        self.assertEqual(lesson["names"], ["Mana Sword", "Major Dyluck", "Pandora"])
        repeated = understand("The Mana Sword is gone! Where is the Sword now, Elder? "
                              "Ask Major Dyluck. Dyluck knows.")
        self.assertEqual(repeated["names"], ["Mana Sword", "Major Dyluck"])
        self.assertTrue(understand("Monsters have been seen!")["warning"])
        self.assertEqual(understand("Never go north.")["directions"], [])

    def test_a_talk_reports_only_what_was_new(self):
        self.add_page("a", "Go east to the cave.", 10, 100)
        first = learned_from_session(self.history.connection, "a")
        self.assertEqual((first["new_lines"], first["first_time"]), (1, True))
        self.add_page("b", "Go east to the cave.", 10, 200)
        again = learned_from_session(self.history.connection, "b")
        self.assertEqual((again["new_lines"], again["first_time"]), (0, False))
        self.assertIsNone(hero_comment(again))
        self.assertEqual(hero_comment(first), "Head east, they say. Worth a look.")
        self.assertIsNone(learned_from_session(self.history.connection, "missing"))

    def test_a_villager_with_nothing_new_is_left_alone_for_a_while(self):
        db = self.history.connection
        self.add_page("a", "Get out of here!", 10, 100, 131, 0xC4, 0x10B)
        self.assertFalse(heard_enough(db, 131, 0xC4, 0x10B, 110))     # one talk only
        self.add_page("b", "Get out of here!", 10, 200, 131, 0xC4, 0x10B)
        self.assertTrue(heard_enough(db, 131, 0xC4, 0x10B, 210))
        self.assertFalse(heard_enough(db, 131, 0xC4, 0x10B, 200 + RETALK_S + 1))
        self.add_page("c", "Something serious is going on!", 5, 300, 131, 0xC4, 0x10B)
        self.assertFalse(heard_enough(db, 131, 0xC4, 0x10B, 310))     # story moved on

    def test_following_a_lead_names_the_place_when_known(self):
        self.assertEqual(follow_comment({"directions": ["south"], "names": ["Gaia's Navel"]}),
                         "Going south, toward Gaia's Navel.")
        self.assertEqual(follow_comment({"directions": ["east"]}),
                         "Going east, like the villager said.")


if __name__ == "__main__":
    unittest.main()

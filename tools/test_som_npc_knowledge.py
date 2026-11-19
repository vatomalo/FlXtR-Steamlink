"""NPC knowledge regressions: offline, no ROM, SSH or Ollama."""
import tempfile
import unittest
from pathlib import Path

from som_dialogue import NpcDialogue
from som_goals import build_goal_prompt, memory_summary, offered_goals
from som_memory import ObservationHistory
from som_npc_knowledge import hear_npc_clues, heard_directions


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
        self.add("Hello there!", when=102)
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

    def test_deduplication_and_bounded_history(self):
        for n in range(10):
            self.add("Go east to the cave", when=n + 1)
        self.add("Find the sage", when=99)
        clues = hear_npc_clues(self.history, 131)
        self.assertEqual(len(clues), 2)
        self.assertEqual(clues[0]["quote"], "Find the sage")
        self.assertEqual(clues[1]["quote"], "Go east to the cave")
        self.assertLessEqual(len(hear_npc_clues(self.history, 131, limit=100)), 4)


if __name__ == "__main__":
    unittest.main()

"""NPC conversation evidence, closed-loop inputs and display-only commentary."""
import sqlite3
import tempfile
import threading
import unittest
from pathlib import Path
from unittest.mock import Mock

from som_dialogue import Commentary, GLYPHS, NpcDialogue, dialogue_line, npc_records, speech_bubble
from som_memory import ObservationHistory, WRAM
from som_planner import MemoryPlanner
from test_som_doors import room


def snapshot(mode="field", text="Hello there!", choice=False, npc=True):
    data = bytearray(room(map_id=131).data)
    data[0xE02C], data[0xE010] = 1, 2
    data[0xE600] = 1 if npc else 0
    data[0xE60B] = 0x80
    data[0xE780] = 0xC0
    data[0xE602:0xE604] = (60).to_bytes(2, "little")
    data[0xE604:0xE606] = (40).to_bytes(2, "little")
    data[0xE63E:0xE640] = (0x450).to_bytes(2, "little")
    data[0xD0] = 0x83 if mode == "event" else 0
    data[0x3A:0x3C] = (0x600).to_bytes(2, "little")
    data[0x1D01:0x1D04] = bytes((0x34, 0x12, 0xE3))
    data[0xA170] = 0x5A if choice else 0
    codes = {v:k for k,v in GLYPHS.items()}
    for i, char in enumerate(text[:33]):
        data[0xA1A4+i] = codes[char]
    return WRAM(data)


class TestNpcDialogue(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.history = ObservationHistory(Path(self.tmp.name)/"dialogue.sqlite")
        self.now = 0.0
        self.comments = Mock()
        self.comments.offer.return_value = True
        self.npc = NpcDialogue(self.history, self.comments, clock=lambda: self.now)
        self.planner = MemoryPlanner(self.history)

    def tearDown(self):
        self.history.close()
        self.tmp.cleanup()

    def test_native_glyph_buffer_decodes_case_and_punctuation(self):
        self.assertEqual(dialogue_line(snapshot("event"))["text"], "Hello there!")
        self.assertEqual(dialogue_line(snapshot("event"))["pointer"], 0xE31234)

    def test_stale_field_text_and_missing_npc_do_not_become_dialogue(self):
        self.assertIsNone(dialogue_line(snapshot()))
        self.assertIsNone(dialogue_line(snapshot("event", npc=False)))

    def test_unknown_glyph_fails_closed(self):
        data = bytearray(snapshot("event").data)
        data[0xA1A4] = 0x42
        self.assertIsNone(dialogue_line(WRAM(data)))

    def test_turbo_resume_state_is_readable_but_never_authorizes_confirm(self):
        data = bytearray(snapshot("event").data)
        data[0xD0] = 0x84
        w = WRAM(data)
        self.assertEqual(dialogue_line(w)["text"], "Hello there!")
        self.npc.target = dict(npc_records(w)[0], map_id=131)
        self.npc.phase, self.npc.started, self.npc.confirms = "dialogue", 0, 0
        self.npc.observe(w, w.observe())
        self.now = 2
        self.npc.observe(w, w.observe())
        self.assertEqual(self.npc.step(w, w.observe(), self.planner)[0], "wait")

    def test_monsters_and_uninitialised_events_are_not_people(self):
        data = bytearray(snapshot().data)
        data[0xE780] = 0
        self.assertEqual(npc_records(WRAM(data)), [])
        data[0xE780] = 0xC0
        data[0xE63E:0xE640] = b"\0\0"
        self.assertEqual(npc_records(WRAM(data)), [])

    def test_npc_pool_extends_beyond_monsters_but_stops_before_graphics_buffers(self):
        data=bytearray(snapshot().data)
        data[0xEC00:0xEE00]=data[0xE600:0xE800]
        data[0xF800:0xFA00]=data[0xE600:0xE800]
        self.assertEqual([n["offset"] for n in npc_records(WRAM(data))],[0x600,0xC00])

    def test_schema_is_versioned_without_changing_history_version(self):
        self.history.connection.execute("PRAGMA user_version=7")
        self.history.connection.commit()
        NpcDialogue(self.history)
        self.assertEqual(self.history.connection.execute("PRAGMA user_version").fetchone(),(7,))
        self.assertEqual(self.history.connection.execute("SELECT version FROM npc_dialogue_schema").fetchall(),[(1,)])

    def test_lines_persist_once_and_one_comment_is_offered_per_conversation(self):
        w = snapshot("event")
        self.npc.observe(w, w.observe())
        self.now = .7
        self.npc.observe(w, w.observe())
        self.now = 1.4
        self.npc.observe(w, w.observe())
        self.assertEqual(self.history.connection.execute(
            "SELECT text FROM npc_dialogue_lines").fetchall(), [("Hello there!",)])
        self.comments.offer.assert_called_once_with(131, "Hello there!")

    def test_native_start_and_acknowledged_non_choice_page(self):
        w = snapshot()
        action = self.npc.consider(w, w.observe(), self.planner)
        self.assertEqual(action[0], "confirm")
        w = snapshot("event")
        self.npc.observe(w, w.observe())
        self.now = 1.6
        self.npc.observe(w, w.observe())
        self.assertEqual(self.npc.step(w, w.observe(), self.planner)[0], "confirm")
        self.now = 3
        self.assertEqual(self.npc.step(w, w.observe(), self.planner)[0], "wait")

    def test_choices_are_never_confirmed(self):
        w = snapshot()
        self.npc.consider(w, w.observe(), self.planner)
        w = snapshot("event", choice=True)
        self.npc.observe(w, w.observe())
        self.now = 2
        action = self.npc.step(w, w.observe(), self.planner)
        self.assertEqual(action[0], "wait")
        self.assertIn("player input", action[1])
        self.assertIsNone(self.npc.target)

    def test_timeout_and_npc_disappearance_abort_without_blind_confirms(self):
        w = snapshot()
        self.npc.consider(w, w.observe(), self.planner)
        self.now = 26
        self.assertEqual(self.npc.step(w, w.observe(), self.planner)[0], "wait")
        self.assertIsNone(self.npc.target)

    def test_known_shopkeeper_is_left_to_existing_macro(self):
        self.history.set_agent_state("shop", {"map_id":131,"target":[3,2]})
        w = snapshot()
        self.assertIsNone(self.npc.consider(w, w.observe(), self.planner))

    def test_bubble_output_fits_two_lines_and_strips_thinking_and_controls(self):
        bubble = speech_bubble("<think>private</think>That is interesting! "*5)
        self.assertTrue(bubble.startswith("^"))
        self.assertNotIn("private", bubble)
        self.assertLessEqual(len(bubble[1:].split("|")), 2)
        self.assertTrue(all(len(line)<=32 for line in bubble[1:].split("|")))

    def test_commentary_does_not_block_and_discards_other_map_results(self):
        entered, release = threading.Event(), threading.Event()
        def generate(text):
            entered.set()
            release.wait(1)
            return "^Interesting!"
        comments = Commentary(generate, clock=lambda: self.now)
        self.assertTrue(comments.offer(131,"Hello"))
        self.assertTrue(entered.wait(1))
        self.assertFalse(comments.offer(131,"Again"))
        self.assertIsNone(comments.poll(131))
        release.set()
        item = comments.results.get(timeout=2)
        comments.results.put(item)
        self.assertIsNone(comments.poll(130))

    def test_ollama_failure_produces_no_action_or_bubble(self):
        def fail(text):
            raise OSError("offline")
        comments = Commentary(fail, clock=lambda: self.now)
        comments.offer(131,"Hello")
        item = comments.results.get(timeout=2)
        comments.results.put(item)
        self.assertIsNone(comments.poll(131))


if __name__ == "__main__":
    unittest.main()

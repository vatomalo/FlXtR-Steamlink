import tempfile
import unittest
from pathlib import Path

import som_play_loop as loop
from som_memory import WRAM, ObservationHistory
from som_menu import MacroFailed
from som_teach_save import teach_save

WRAM_SIZE = 0x20000
ALL = lambda map_id: True  # noqa: E731


def summary(hp=100, max_hp=100, gold=0, map_id=131):
    return {"hp": hp, "max_hp": max_hp, "gold": gold, "map_id": map_id}


class History:
    """Minimal agent_state + reward_ledger stand-in."""

    def __init__(self, rewards=()):
        import sqlite3
        self.connection = sqlite3.connect(":memory:")
        self.connection.execute("CREATE TABLE reward_ledger (map_id, gold_gained, hp_lost)")
        self.connection.executemany("INSERT INTO reward_ledger VALUES (?,?,?)", rewards)
        self.state = {}

    def get_agent_state(self, key):
        return self.state.get(key)

    def set_agent_state(self, key, value):
        self.state[key] = dict(value)


class PlayLoopTest(unittest.TestCase):
    def make(self, rewards=((129, 676, 588), (130, 40, 194)), **kw):
        self.history = History(rewards)
        self.logs = []
        return loop.PlayLoop(self.history, log=self.logs.append, **kw)

    def test_hunts_best_gold_per_hp_map_even_with_negative_score(self):
        play = self.make()
        self.assertEqual(play.goal(summary(), True, True, True, ALL), "hunt_map_129")
        self.assertEqual(play.goal(summary(map_id=129), True, True, True, ALL), "hunt_here")

    def test_unreachable_or_fixed_hunt_map(self):
        play = self.make()
        self.assertEqual(play.goal(summary(), False, True, True, lambda m: m != 129),
                         "hunt_map_130")
        play = self.make(hunt_map=130)
        self.assertEqual(play.goal(summary(), False, True, True, ALL), "hunt_map_130")

    def test_no_hunting_memory_hunts_here(self):
        play = self.make(rewards=())
        self.assertEqual(play.goal(summary(), False, True, True, ALL), "hunt_here")

    def test_full_cycle(self):
        play = self.make(trip_gold=100)
        play.goal(summary(gold=50, map_id=129), True, True, True, ALL)
        self.assertEqual(play.goal(summary(gold=160, hp=80, map_id=129), True, True, True, ALL),
                         "go_to_shop")
        self.assertEqual(play.phase, "shop")
        self.assertEqual(play.goal(summary(gold=100, hp=80), False, True, True, ALL),
                         "go_to_inn")
        play.inn_done()
        self.assertEqual(play.goal(summary(), False, True, True, ALL), "go_to_save")
        play.save_done()
        self.assertEqual(play.phase, "hunt")
        self.assertEqual(play.state["cycles"], 1)
        self.assertEqual(self.history.state["play_loop"]["phase"], "hunt")

    def test_low_hp_ends_trip(self):
        play = self.make()
        self.assertEqual(play.goal(summary(hp=55, map_id=129), False, True, True, ALL),
                         "go_to_inn")

    def test_hunt_time_limit(self):
        play = self.make(hunt_decisions=5)
        goals = [play.goal(summary(map_id=129), False, True, False, ALL) for _ in range(6)]
        self.assertEqual(goals[:4], ["hunt_here"] * 4)
        self.assertEqual(play.phase, "hunt")          # full HP, no save: back to hunting
        self.assertEqual(play.state["cycles"], 1)

    def test_full_hp_skips_inn_and_missing_save_skips_save(self):
        play = self.make()
        play._enter("shop", "test")
        self.assertEqual(play.goal(summary(), False, True, False, ALL), "hunt_map_129")
        self.assertTrue(any("no save taught" in line for line in self.logs))
        self.assertTrue(any("HP already full" in line for line in self.logs))

    def test_unusable_inn_goes_to_save(self):
        play = self.make()
        play._enter("inn", "test")
        self.assertEqual(play.goal(summary(hp=40), False, False, True, ALL), "go_to_save")

    def test_errand_timeout_moves_on(self):
        play = self.make()
        play._enter("shop", "test")
        for _ in range(loop.ERRAND_DECISIONS):
            self.assertEqual(play.goal(summary(hp=80), True, True, True, ALL), "go_to_shop")
        self.assertEqual(play.goal(summary(hp=80), True, True, True, ALL), "go_to_inn")

    def test_failures_advance(self):
        play = self.make()
        play._enter("inn", "test")
        play.inn_failed("no gold")
        self.assertEqual(play.phase, "save")
        play.save_failed("page missing")
        self.assertEqual(play.phase, "hunt")

    def test_phase_survives_restart(self):
        play = self.make()
        play._enter("inn", "test")
        again = loop.PlayLoop(self.history, log=self.logs.append)
        self.assertEqual(again.phase, "inn")


def screen(mode="field", text=b"", tile_ok=True):
    raw = bytearray(WRAM_SIZE)
    raw[0xE000] = 1
    raw[0x00E4], raw[0x00E6] = 1, 0x0F
    if mode == "event":
        raw[0x00D0] = 0x83
        raw[0xA1A4:0xA1A4 + len(text)] = text
    return WRAM(bytes(raw))


SAVED = bytes([0x9B + 18, 0x81, 0x96, 0x85, 0x84])          # "Saved"
SAVE = {"map_id": 259, "layer": 2, "target": [20, 13], "face": "step_up",
        "buttons": ["confirm", "confirm", "confirm"], "final_page": "Saved", "taught": True}


class Link:
    def __init__(self, frames):
        self.frames, self.sent = list(frames), []

    def send_action(self, action):
        self.sent.append(action)

    def fetch_wram(self, _marker):
        return (self.frames.pop(0) if len(self.frames) > 1 else self.frames[0]).data


class SaveMacroTest(unittest.TestCase):
    def macro(self, frames, anchor=SAVE):
        link = Link(frames)
        macro = loop.SaveMacro(link, anchor, sleep=lambda _: None, log=lambda _: None)
        macro._tile = lambda wram: (259, 2, 20, 13)
        return macro, link

    def test_page_text(self):
        self.assertEqual(loop.page_text(screen("event", SAVED)), "Saved")
        self.assertIsNone(loop.page_text(screen()))

    def test_replays_and_verifies_final_page(self):
        frames = [screen(), screen(), screen("event", b"\x81\x82"), screen("event", SAVED),
                  screen(), screen()]
        macro, link = self.macro(frames)
        self.assertEqual(macro.run(), ["ab", "Saved"])
        self.assertEqual(link.sent, ["step_up", "confirm", "confirm", "confirm"])

    def test_missing_final_page_fails(self):
        frames = [screen(), screen(), screen("event", b"\x81\x82"), screen("event", b"\x81"),
                  screen(), screen()]
        macro, _ = self.macro(frames)
        with self.assertRaises(MacroFailed):
            macro.run()

    def test_dialog_must_start(self):
        macro, _ = self.macro([screen()])
        with self.assertRaisesRegex(MacroFailed, "did not start"):
            macro.run()

    def test_untaught_is_refused(self):
        macro, link = self.macro([screen()], anchor=dict(SAVE, final_page=None))
        with self.assertRaisesRegex(MacroFailed, "teach_save"):
            macro.run()
        self.assertEqual(link.sent, [])


class TeachSaveTest(unittest.TestCase):
    def test_validation_and_storage(self):
        with tempfile.TemporaryDirectory() as tmp:
            history = ObservationHistory(Path(tmp) / "h.sqlite")
            history._verified_position = lambda state, _: {"map_id": 259, "layer": 2,
                                                           "target": [20, 13]}
            with self.assertRaises(ValueError):
                teach_save(history, {}, "step_up", ["cancel"], ["Saved"])
            with self.assertRaises(ValueError):
                teach_save(history, {}, "step_up", ["confirm", "start"], ["Saved"])
            with self.assertRaises(ValueError):
                teach_save(history, {}, "step_up", ["confirm"], [])
            save = teach_save(history, {}, "step_up", ["confirm", "confirm"], ["Hi", "Saved"])
            self.assertEqual(save["final_page"], "Saved")
            self.assertEqual(history.get_agent_state("save")["buttons"], ["confirm", "confirm"])
            history.close()


if __name__ == "__main__":
    unittest.main()

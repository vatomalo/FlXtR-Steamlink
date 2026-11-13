"""Candy macro must not leave the AI waiting forever on a lingering ring."""
import unittest
from som_menu import CandyMacro, MacroFailed


class FakeWram:
    def __init__(self, mode):
        self.mode = mode

    def game_mode(self):
        return self.mode

    def u8(self, address):
        return 1 if self.mode == "ring" else 0


class FakeLink:
    def __init__(self, modes):
        self.modes = list(modes)
        self.presses = []

    def fetch_wram(self, active):
        mode = self.modes[0]
        if len(self.modes) > 1:
            self.modes.pop(0)
        # _read() always passes raw memory to WRAM; override it in tests.
        return FakeWram(mode)

    def send_action(self, action):
        self.presses.append(action)


class TestCandyClose(unittest.TestCase):
    def make(self, modes):
        link = FakeLink(modes)
        cal = type("Cal", (), {"cancel_action": "cancel"})()
        macro = CandyMacro(link, cal, sleep=lambda _: None)
        macro._read = lambda: link.fetch_wram(True)
        return macro, link

    def test_close_after_more_than_four_cancels(self):
        macro, link = self.make(["ring"] * 7 + ["field"])
        self.assertTrue(macro._close_ring())
        self.assertEqual(link.presses, ["cancel"] * 7)

    def test_never_confirm_unrelated_dialog(self):
        macro, link = self.make(["ring", "event"])
        with self.assertRaisesRegex(MacroFailed, "not an item ring"):
            macro._close_ring()
        self.assertEqual(link.presses, ["cancel"])

    def test_bounded_failure_when_ring_never_closes(self):
        macro, link = self.make(["ring"])
        with self.assertRaisesRegex(MacroFailed, "still open"):
            macro._close_ring()
        self.assertEqual(len(link.presses), 12)

    def test_already_closed(self):
        macro, link = self.make(["field"])
        self.assertTrue(macro._close_ring())
        self.assertEqual(link.presses, [])


if __name__ == "__main__":
    unittest.main()

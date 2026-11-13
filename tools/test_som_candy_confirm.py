"""Verify Candy needs a released third confirm before recovery."""
import unittest
from som_menu import CandyMacro, RING_KIND, RING_OPEN, MacroFailed


class FakeWRAM:
    def __init__(self, link):
        self.link = link

    def game_mode(self):
        return "ring" if self.link.in_ring else "field"

    def u8(self, address):
        if address == RING_OPEN:
            return int(self.link.in_ring)
        if address == RING_KIND:
            return 3
        return 0

    def character(self, index):
        return {"hp": 48 + (30 if self.link.consumed else 0)}

    def inventory(self):
        return [{"name": "candy", "verified": True, "slot": 0,
                 "count": 1 - int(self.link.consumed)}]


class FakeLink:
    def __init__(self, required_confirms=3):
        self.actions = []
        self.in_ring = False
        self.consumed = False
        self.confirms = 0
        self.held = False
        self.required_confirms = required_confirms

    def send_action(self, action):
        self.actions.append(action)
        if action == "open_ring":
            self.in_ring = True
        elif action == "confirm":
            if not self.held:
                self.confirms += 1
                if self.confirms >= self.required_confirms:
                    self.consumed = True
            self.held = True
        elif action == "wait":
            self.held = False
        elif action == "cancel":
            self.in_ring = False


class TestCandyConfirm(unittest.TestCase):
    def macro(self, count):
        link = FakeLink(count)
        cal = type("Calibration", (), {
            "open_action": "open_ring", "cancel_action": "cancel",
            "confirm_action": "confirm", "item_ring_kind": 3,
            "cursor_address": None, "candy_cursor": 0, "candy_slot": 0
        })()
        macro = CandyMacro(link, cal, sleep=lambda _: None)
        macro._read = lambda: FakeWRAM(link)
        macro.ready = lambda _: None
        return macro, link

    def test_three_confirmations_with_release(self):
        macro, link = self.macro(3)
        self.assertEqual(macro.run(), 30)
        self.assertEqual(link.actions.count("confirm"), 3)
        self.assertIn("wait", link.actions)
        self.assertEqual(link.actions[-1], "cancel")

    def test_stop_when_success_after_two(self):
        macro, link = self.macro(2)
        self.assertEqual(macro.run(), 30)
        self.assertEqual(link.actions.count("confirm"), 2)

    def test_refuse_endless_confirms(self):
        macro, link = self.macro(4)
        with self.assertRaisesRegex(MacroFailed, "3 Candy confirmations"):
            macro.run()
        self.assertEqual(link.actions.count("confirm"), 3)
        self.assertFalse(link.in_ring)


if __name__ == "__main__":
    unittest.main()

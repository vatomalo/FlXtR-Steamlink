"""Ring-menu macro tests against a simulated ring; no emulator required."""
import unittest

from som_menu import (RING_KIND, RING_OPEN, CandyMacro, Calibration, MacroFailed,
                      find_cursor_candidates)
from som_memory import WRAM

CURSOR = 0x7EA21C
HERO_HP = 0x7EE000 + 0x182


class FakeGame:
    """Tiny ring-menu simulation driven by the same action names as the proxy."""

    def __init__(self, items=(0x20, 0x41, 0x23), opener="open_ring", heal=True):
        self.ram = bytearray(0x20000)
        self.set(0x7EE000, 1)                  # hero slot present
        self.set(0x7E00E4, 1); self.set(0x7E00E6, 0x0F)  # field mode
        self.set16(HERO_HP, 40); self.set16(HERO_HP + 2, 100)
        for slot in range(11):
            self.set(0x7ECC48 + slot, items[slot] if slot < len(items) else 0xFF)
        self.items = len(items)
        self.opener, self.heal = opener, heal
        self.sent = []

    def set(self, address, value):
        self.ram[address - 0x7E0000] = value

    def set16(self, address, value):
        self.set(address, value & 0xFF); self.set(address + 1, value >> 8)

    def get(self, address):
        return self.ram[address - 0x7E0000]

    # RemoteLink interface
    def fetch_wram(self, require_marker=False):
        return bytes(self.ram)

    def send_action(self, action):
        self.sent.append(action)
        is_open = self.get(RING_OPEN) == 1
        if action == self.opener and not is_open:
            self.set(RING_OPEN, 1); self.set(RING_KIND, 0)
            self.confirms = 0
        elif action == "cancel" and is_open:
            self.set(RING_OPEN, 0)
        elif action == "step_right" and is_open:
            self.set(CURSOR, (self.get(CURSOR) + 1) % self.items)
        elif action == "confirm" and is_open:
            self.confirms += 1
            if self.confirms == 2 and self.heal and self.get(CURSOR) == 0:
                self.set16(HERO_HP, 100)
                self.set(0x7ECC48, 0x00 | 0x00)  # count 1 -> code 0 (used up)
                self.set(0x7ECC48, 0xFF)
                self.set(RING_OPEN, 0)


def calibration(**overrides):
    data = {"open_action": "open_ring", "item_ring_kind": 0,
            "cursor_address": CURSOR, "cursor_step": 1,
            "candy_cursor": 0, "candy_slot": 0}
    data.update(overrides)
    return Calibration(data)


def run(game, cal=None):
    now = [0.0]
    def sleep(seconds):
        now[0] += seconds
    return CandyMacro(game, cal or calibration(), log=lambda _: None,
                      sleep=sleep, clock=lambda: now[0]).run()


class TestRingCalibration(unittest.TestCase):
    def test_finds_the_byte_that_advances_once_per_rotation(self):
        game = FakeGame(items=(0x20, 0x41, 0x23, 0x25))
        game.set(RING_OPEN, 1)
        snapshots = []
        for _ in range(5):
            snapshots.append(WRAM(bytes(game.ram)))
            game.set(CURSOR, (game.get(CURSOR) + 1) % 4)
        self.assertIn((CURSOR, 1), find_cursor_candidates(snapshots, 4))


class TestCandyMacro(unittest.TestCase):
    def test_rotates_to_candy_uses_it_and_verifies_hp(self):
        game = FakeGame()
        game.set(CURSOR, 1)          # cursor starts on the herb
        self.assertEqual(run(game), 60)
        self.assertEqual(game.sent[0], "open_ring")
        self.assertIn("step_right", game.sent)
        self.assertEqual(game.sent.count("confirm"), 2)
        self.assertEqual(game.get(RING_OPEN), 0)

    def test_wrong_open_button_fails_closed(self):
        game = FakeGame(opener="menu_x")
        with self.assertRaises(MacroFailed):
            run(game)
        self.assertNotIn("confirm", game.sent)

    def test_no_healing_closes_ring_and_fails(self):
        game = FakeGame(heal=False)
        with self.assertRaises(MacroFailed):
            run(game)
        self.assertEqual(game.get(RING_OPEN), 0)
        self.assertIn("cancel", game.sent)

    def test_refuses_when_candy_moved(self):
        game = FakeGame(items=(0x41, 0x20))
        with self.assertRaises(MacroFailed) as raised:
            run(game)
        self.assertIn("recalibrate", str(raised.exception))
        self.assertEqual(game.sent, [])

    def test_wrong_ring_kind_fails_before_selecting(self):
        game = FakeGame()
        with self.assertRaises(MacroFailed):
            run(game, calibration(item_ring_kind=3))
        self.assertNotIn("confirm", game.sent)


if __name__ == "__main__":
    unittest.main()

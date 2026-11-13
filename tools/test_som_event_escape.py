import unittest

import som_event_escape as esc
from som_memory import WRAM

WRAM_SIZE = 0x20000


class Clock:
    def __init__(self):
        self.t = 0.0

    def __call__(self):
        return self.t


def wram(mode="event", d0=0x83, text=b"\x81\x82", pointer=0x1234, choice=False):
    raw = bytearray(WRAM_SIZE)
    if mode != "no_game":
        raw[0xE000] = 1
        raw[0x00E4], raw[0x00E6] = 1, 0x0F
    if mode == "event":
        raw[0x00D0] = d0
        raw[0x1D01], raw[0x1D02], raw[0x1D03] = pointer & 0xFF, pointer >> 8 & 0xFF, 0xC9
        raw[0xA1A4:0xA1A4 + len(text)] = text
        if choice:
            raw[0xA170] = 0x5A
    return WRAM(bytes(raw))


class EventEscapeTest(unittest.TestCase):
    def setUp(self):
        self.clock = Clock()
        self.logs = []
        self.escape = esc.EventEscape(self.clock, self.logs.append)

    def run_for(self, seconds, screen, step=0.1):
        actions = []
        end = self.clock.t + seconds
        while self.clock.t < end:
            result = self.escape.observe(screen)
            if result:
                actions.append(result[0])
            self.clock.t += step
        return actions

    def test_field_mode_never_presses(self):
        self.assertEqual(self.run_for(5, wram("field")), [])

    def test_waits_for_settle_then_confirms_text(self):
        screen = wram()
        self.assertEqual(self.run_for(0.95, screen), [])
        self.assertEqual(self.run_for(0.2, screen), ["confirm"])

    def test_presses_are_spaced(self):
        actions = self.run_for(1.0 + 3.0, wram())
        self.assertLessEqual(len(actions), int(3.0 / esc.GAP) + 1)
        self.assertGreaterEqual(len(actions), 3)

    def test_stuck_page_alternates_confirm_and_cancel(self):
        actions = self.run_for(6, wram())
        self.assertEqual(actions[:2], ["confirm", "confirm"])
        self.assertIn("cancel", actions[2:])
        self.assertIn("confirm", actions[2:])

    def test_new_page_resets_settle(self):
        self.run_for(1.1, wram(pointer=1))
        self.assertIsNone(self.escape.observe(wram(pointer=2)))
        self.assertEqual(self.run_for(0.9, wram(pointer=2)), [])

    def test_choice_is_only_cancelled(self):
        actions = self.run_for(20, wram(choice=True))
        self.assertTrue(actions)
        self.assertEqual(set(actions), {"cancel"})

    def test_unknown_event_state_is_only_cancelled(self):
        actions = self.run_for(20, wram(d0=0x05, text=b""))
        self.assertTrue(actions)
        self.assertEqual(set(actions), {"cancel"})

    def test_bounded_presses_then_gives_up(self):
        actions = self.run_for(120, wram())
        self.assertEqual(len(actions), esc.MAX_PRESSES)
        self.assertTrue(any("leaving it to the player" in line for line in self.logs))

    def test_leaving_event_resets_budget(self):
        self.run_for(120, wram())
        self.escape.observe(wram("field"))
        self.assertTrue(self.run_for(2, wram()))

    def test_transition_is_not_an_event(self):
        self.assertEqual(self.run_for(5, wram("transition")), [])


if __name__ == "__main__":
    unittest.main()

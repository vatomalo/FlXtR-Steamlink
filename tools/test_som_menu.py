"""Ring-menu macro tests against a simulated ring; no emulator required."""
import unittest

from som_menu import (RING_KIND, RING_OPEN, CandyMacro, Calibration, InnMacro,
                      MacroFailed, ShopMacro, find_cursor_candidates, find_gold_candidates,
                      narrow_gold, read_gold)
from som_memory import WRAM

CURSOR = 0x7EA21C
HERO_HP = 0x7EE000 + 0x182


class FakeGame:
    """Tiny ring-menu simulation driven by the same action names as the proxy."""

    def __init__(self, items=(0x20, 0x41, 0x23), opener="open_ring", heal=True, rings=(0,)):
        self.ram = bytearray(0x20000)
        self.set(0x7EE000, 1)                  # hero slot present
        self.set(0x7E00E4, 1); self.set(0x7E00E6, 0x0F)  # field mode
        self.set16(HERO_HP, 40); self.set16(HERO_HP + 2, 100)
        for slot in range(11):
            self.set(0x7ECC48 + slot, items[slot] if slot < len(items) else 0xFF)
        self.items = len(items)
        self.opener, self.heal = opener, heal
        self.rings = rings          # ring kinds in Up order; opens on the first
        self.drop_ups = 0
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
            self.set(RING_OPEN, 1); self.set(RING_KIND, self.rings[0])
            self.confirms = 0
        elif action == "step_up" and is_open and self.drop_ups:
            self.drop_ups -= 1                  # live Turbo drops some ring presses
        elif action == "step_up" and is_open:
            kind = self.rings.index(self.get(RING_KIND))
            self.set(RING_KIND, self.rings[(kind + 1) % len(self.rings)])
        elif action == "cancel" and is_open:
            self.set(RING_OPEN, 0)
        elif action == "step_right" and is_open:
            self.set(CURSOR, (self.get(CURSOR) + 1) % self.items)
        elif action == "confirm" and is_open:
            self.confirms += 1
            if (self.confirms == 2 and self.heal and self.get(CURSOR) == 0 and
                    self.get(RING_KIND) == 0):
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

    def test_single_item_ring_needs_no_rotation(self):
        game = FakeGame(items=(0x20,))
        self.assertEqual(run(game, calibration(cursor_address=None, item_types=1)), 60)
        self.assertNotIn("step_right", game.sent)

    def test_single_item_calibration_refuses_after_new_item_type(self):
        game = FakeGame(items=(0x20, 0x41))
        with self.assertRaises(MacroFailed) as raised:
            run(game, calibration(cursor_address=None, item_types=1))
        self.assertIn("rerun", str(raised.exception))
        self.assertEqual(game.sent, [])

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

    def test_presses_up_from_another_ring_to_the_item_ring(self):
        # Live: "ring opened on kind 1, not the item ring" disabled Candy.
        game = FakeGame(rings=(1, 2, 0))
        self.assertEqual(run(game), 60)
        self.assertEqual(game.sent[:3], ["open_ring", "step_up", "step_up"])

    def test_a_dropped_up_press_is_pressed_again(self):
        # Live order with the solo hero: sword 3 -> items 0 (equip 2 between).
        game = FakeGame(rings=(3, 0, 2))
        game.drop_ups = 1
        self.assertEqual(run(game), 60)
        self.assertEqual(game.sent.count("step_up"), 2)

    def test_wrong_ring_kind_fails_before_selecting(self):
        game = FakeGame()
        with self.assertRaises(MacroFailed):
            run(game, calibration(item_ring_kind=3))
        self.assertNotIn("confirm", game.sent)


EVENT = 0x7E00D0
TILE_X = 0x7EF000   # test-only stand-in for the verified actor tile


class FakeInn(FakeGame):
    """Innkeeper dialog: talk, a few confirms, HP restored, dialog closes."""

    def __init__(self, heals_at=3, closes_at=5, face_moves=False, talks=True):
        super().__init__()
        self.heals_at, self.closes_at = heals_at, closes_at
        self.face_moves, self.talks = face_moves, talks
        self.dialog_confirms = 0

    def send_action(self, action):
        self.sent.append(action)
        in_dialog = self.get(EVENT) != 0
        if action.startswith("step_") and self.face_moves and not in_dialog:
            self.set(TILE_X, self.get(TILE_X) + 1)
        elif action == "confirm" and not in_dialog and self.talks:
            self.set(EVENT, 1)
        elif action == "confirm" and in_dialog:
            self.dialog_confirms += 1
            if self.dialog_confirms == self.heals_at:
                self.set16(HERO_HP, 100)
            if self.dialog_confirms >= self.closes_at:
                self.set(EVENT, 0)
        elif action == "cancel" and in_dialog:
            self.set(EVENT, 0)


class TestInn(InnMacro):
    def _tile(self, wram):
        return (131, 1, wram.u8(TILE_X), 4)


ANCHOR = {"map_id": 131, "layer": 1, "target": [0, 4], "face": "step_up", "taught": True}


def run_inn(game, anchor=ANCHOR):
    now = [0.0]
    def sleep(seconds):
        now[0] += seconds
    return TestInn(game, anchor, log=lambda _: None, sleep=sleep,
                   clock=lambda: now[0]).run()


class TestInnMacro(unittest.TestCase):
    def test_talks_confirms_and_verifies_full_hp(self):
        game = FakeInn()
        self.assertEqual(run_inn(game), 60)
        self.assertEqual(game.sent[0], "step_up")
        self.assertEqual(game.get(EVENT), 0)
        self.assertNotIn("cancel", game.sent)

    def test_facing_tap_that_moves_the_hero_aborts_before_talking(self):
        game = FakeInn(face_moves=True)
        with self.assertRaises(MacroFailed):
            run_inn(game)
        self.assertNotIn("confirm", game.sent)

    def test_no_dialog_fails(self):
        with self.assertRaises(MacroFailed) as raised:
            run_inn(FakeInn(talks=False))
        self.assertIn("did not start talking", str(raised.exception))

    def test_dialog_without_healing_cancels_out_and_fails(self):
        game = FakeInn(heals_at=99, closes_at=99)
        with self.assertRaises(MacroFailed):
            run_inn(game)
        self.assertIn("cancel", game.sent)
        self.assertEqual(game.get(EVENT), 0)
        self.assertLessEqual(game.sent.count("confirm"), InnMacro.MAX_CONFIRMS + 1)

    def test_requires_taught_tile_and_facing(self):
        with self.assertRaises(MacroFailed):
            run_inn(FakeInn(), dict(ANCHOR, target=[3, 4]))
        with self.assertRaises(MacroFailed):
            run_inn(FakeInn(), {"map_id": 131, "layer": 1, "target": [0, 4]})

class FakeSavingInn(FakeInn):
    """Live Turbo inn: after the night a Save? choice that cancel cannot close,
    then a slot list (cursor 7E:A211), then a one-page message."""

    def __init__(self, start_cursor=0, drop_first_slot_confirm=False):
        super().__init__(heals_at=3, closes_at=99)
        self.stage = "stay"
        self.saved_slot = None
        self.start_cursor = start_cursor
        self.drop = drop_first_slot_confirm

    def send_action(self, action):
        if self.stage == "stay":
            super().send_action(action)
            if self.get(HERO_HP) == 100:
                self.stage = "prompt"
                self.set(0x7EA170, 0x5A)
            return
        self.sent.append(action)
        if self.stage == "prompt" and action == "confirm":
            self.stage = "slots"
            self.set(0x7EA170, 0)
            self.set(SAVE_CURSOR, self.start_cursor)
        elif self.stage == "slots" and action == "step_down":
            self.set(SAVE_CURSOR, min(3, self.get(SAVE_CURSOR) + 1))
        elif self.stage == "slots" and action == "step_up":
            self.set(SAVE_CURSOR, max(0, self.get(SAVE_CURSOR) - 1))
        elif self.stage == "slots" and action == "confirm":
            if self.drop:
                self.drop = False
                return
            self.saved_slot = self.get(SAVE_CURSOR) + 1
            self.stage = "message"
            self.set(0x7EA1A4, 0xB5)
        elif self.stage == "message" and action == "confirm":
            self.stage = "done"
            self.set(EVENT, 0)


SAVE_CURSOR = 0x7EA211


class TestInnSave(unittest.TestCase):
    def run_saving(self, game, slot):
        now = [0.0]
        def sleep(seconds):
            now[0] += seconds
        inn = TestInn(game, ANCHOR, log=lambda _: None, sleep=sleep,
                      clock=lambda: now[0], save_slot=slot)
        return inn, inn.run()

    def test_answers_yes_and_saves_into_the_usual_slot(self):
        game = FakeSavingInn()
        inn, gained = self.run_saving(game, 1)
        self.assertEqual((gained, game.saved_slot, inn.saved), (60, 1, True))
        self.assertEqual(game.get(EVENT), 0)
        self.assertNotIn("cancel", game.sent)

    def test_moves_the_cursor_to_the_slot_and_retries_a_dropped_press(self):
        game = FakeSavingInn(start_cursor=2, drop_first_slot_confirm=True)
        inn, _ = self.run_saving(game, 1)
        self.assertEqual(game.saved_slot, 1)
        # One facing tap toward the innkeeper, then two cursor moves.
        self.assertEqual(game.sent.count("step_up"), 3)

    def test_without_a_save_slot_the_prompt_is_never_answered(self):
        game = FakeSavingInn()
        inn, _ = self.run_saving(game, None)
        self.assertIsNone(game.saved_slot)
        self.assertFalse(inn.saved)


GOLD = 0x7ECC6A


class FakeShop(FakeInn):
    """Shop dialog: talk, then the taught presses buy one Candy for 10 GP."""

    def __init__(self, sells=True, candy=0x20, gold=600, confirm_at=2):
        super().__init__()
        self.set(0x7ECC48, candy)
        self.set16(GOLD, gold & 0xFFFF); self.set(GOLD + 2, gold >> 16)
        self.sells, self.confirm_at = sells, confirm_at

    def send_action(self, action):
        self.sent.append(action)
        in_dialog = self.get(EVENT) != 0
        if action.startswith("step_") and not in_dialog:
            return
        if action == "confirm" and not in_dialog:
            self.set(EVENT, 1); self.dialog_confirms = 0
        elif action == "confirm" and in_dialog:
            self.dialog_confirms += 1
            if self.dialog_confirms == self.confirm_at and self.sells:
                self.set(0x7ECC48, self.get(0x7ECC48) + 0x20)   # one more Candy (count in high nibble)
                self.set16(GOLD, self.get(GOLD) | self.get(GOLD + 1) << 8)
                gold = (self.get(GOLD) | self.get(GOLD + 1) << 8) - 10
                self.set16(GOLD, gold)
        elif action == "cancel" and in_dialog:
            self.set(EVENT, 0)


class TestShop(ShopMacro):
    def _tile(self, wram):
        return (131, 1, wram.u8(TILE_X), 4)


SHOP = dict(ANCHOR, face="step_left",
            buttons=["confirm", "step_down", "confirm", "confirm", "cancel"], price=10)
GOLD_AT = {"address": GOLD, "width": 3}


def run_shop(game, shop=SHOP):
    now = [0.0]
    def sleep(seconds):
        now[0] += seconds
    return TestShop(game, shop, GOLD_AT, log=lambda _: None, sleep=sleep,
                    clock=lambda: now[0]).run()


class TestGoldAndShop(unittest.TestCase):
    def test_two_readings_pin_down_the_gold_location(self):
        game = FakeShop(gold=1234)
        candidates = find_gold_candidates(WRAM(bytes(game.ram)), 1234)
        self.assertIn((GOLD, 3), candidates)
        game.set16(GOLD, 1224)
        found = narrow_gold(candidates, WRAM(bytes(game.ram)), 1224)
        self.assertEqual({address for address, _ in found}, {GOLD})
        self.assertEqual(read_gold(WRAM(bytes(game.ram)), GOLD_AT), 1224)

    def test_replays_purchase_and_verifies_candy_and_gold(self):
        game = FakeShop()
        self.assertEqual(run_shop(game), 10)
        self.assertEqual(game.sent[0], "step_left")
        self.assertEqual(game.sent[1:], SHOP["buttons"])
        self.assertEqual(game.get(EVENT), 0)

    def test_extra_buy_confirmation_before_exit(self):
        game = FakeShop(confirm_at=3)
        self.assertEqual(run_shop(game), 10)
        self.assertEqual(game.sent.count("confirm"), 4)
        self.assertEqual(game.sent[-1], "cancel")
        self.assertEqual(game.get(EVENT), 0)

    def test_live_recording_stops_at_cancel_and_confirms_up_to_twice_more(self):
        # Taught live: 7 confirms, 2 cancels, then 3 ring presses in the field.
        taught = dict(SHOP, buttons=["confirm"] * 7 + ["cancel"] * 2 + ["open_ring"] * 3)
        game = FakeShop(confirm_at=8)       # needs two presses beyond the six in-shop ones
        self.assertEqual(run_shop(game, taught), 10)
        self.assertEqual(game.sent.count("confirm"), 9)
        self.assertNotIn("open_ring", game.sent)
        self.assertEqual(game.get(EVENT), 0)
        game = FakeShop(confirm_at=9)       # three more would be needed: give up
        with self.assertRaises(MacroFailed):
            run_shop(game, taught)
        self.assertEqual(game.sent.count("confirm"), 9)

    def test_purchase_not_seen_cancels_out_and_fails(self):
        game = FakeShop(sells=False)
        with self.assertRaises(MacroFailed) as raised:
            run_shop(game)
        self.assertIn("purchase not observed", str(raised.exception))
        self.assertEqual(game.get(EVENT), 0)

    def test_refuses_with_four_candy_or_too_little_gold(self):
        with self.assertRaises(MacroFailed):
            run_shop(FakeShop(candy=0x80))
        with self.assertRaises(MacroFailed):
            run_shop(FakeShop(gold=5))

    def test_refuses_unreplayable_recording(self):
        game = FakeShop()
        with self.assertRaises(MacroFailed):
            run_shop(game, dict(SHOP, buttons=["confirm", "start"]))
        self.assertEqual(game.sent, [])

if __name__ == "__main__":
    unittest.main()

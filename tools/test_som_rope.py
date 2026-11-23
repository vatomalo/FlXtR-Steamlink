import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import som_rope
from som_memory import WRAM
from som_rope import RopeTactics, rope_ready


def wram(allowed=0x60, target=0x14A, bag=(0xE4, 0x26)):
    ram = bytearray(0x20000)
    ram[0xCC48:0xCC53] = b"\xff" * 11
    ram[0xCC48:0xCC48 + len(bag)] = bytes(bag)
    ram[0x11F], ram[0x108], ram[0x109] = allowed, target & 255, target >> 8
    return WRAM(bytes(ram))


class RopeTest(unittest.TestCase):
    def setUp(self):
        self.now = 0.0
        self.rope = RopeTactics(clock=lambda: self.now)

    def walk(self, maps, step=30.0):
        for m in maps:
            self.now += step
            self.rope.see_map(m)

    def ask(self, map_id=314, intention="explore_frontier", contact=False, enemies=(), w=None):
        return self.rope.decide(w or wram(), map_id, intention, contact, enemies)

    def test_items_read_in_turbos_order(self):
        names = [i["name"] for i in wram(bag=(0xE4, 0x26, 0xE5, 0xA1, 0xE2, 0x40)).inventory()]
        self.assertEqual(names, ["medical_herb", "magic_rope", "cup_of_wishes", "chocolate",
                                 "royal_jam", "candy"])

    def test_ready_needs_rope_permission_and_a_way_out(self):
        self.assertTrue(rope_ready(wram()))
        self.assertFalse(rope_ready(wram(allowed=0x20)))
        self.assertFalse(rope_ready(wram(target=0)))
        self.assertFalse(rope_ready(wram(bag=(0xE4,))))

    def test_the_live_circle_uses_the_rope(self):
        self.walk([314, 276, 314, 269, 314, 276, 314, 269, 314, 276, 314])
        self.assertIn("circling", self.ask())
        self.assertIsNone(self.ask(contact=True))
        self.assertIsNone(self.ask(enemies=[{"identity": 0x60}]))
        self.assertIsNone(self.ask(w=wram(allowed=0)))

    def test_new_maps_and_quick_hops_are_not_circling(self):
        self.walk([1, 2, 3, 4, 5, 6, 7, 8, 9])
        self.assertIsNone(self.ask(map_id=9))
        self.rope = RopeTactics(clock=lambda: self.now)
        self.walk([314, 276] * 6, step=5.0)
        self.assertIsNone(self.ask())

    def test_errand_and_no_repeat(self):
        self.walk([312, 314])
        self.assertIn("return_to_inn", self.ask(intention="return_to_inn"))
        self.rope.used(314)
        self.assertIsNone(self.ask(intention="return_to_inn"))
        self.now += 3
        self.assertIn("now on map 312", self.rope.see_map(312))
        self.now += som_rope.COOLDOWN_S + 1
        self.assertIsNone(self.ask(map_id=312, intention="return_to_inn"))   # its landing
        self.rope.see_map(314)
        self.assertIsNotNone(self.ask(intention="return_to_inn"))

    def test_a_use_that_does_not_move_us_waits_long(self):
        self.walk([314])
        self.rope.used(314)
        self.now += som_rope.CONFIRM_S + 1
        self.assertIn("still on map 314", self.rope.see_map(314))
        self.now += som_rope.COOLDOWN_S + 1
        self.assertIsNone(self.ask(intention="return_to_inn"))
        self.now += som_rope.REFUSED_S
        self.assertIsNotNone(self.ask(intention="return_to_inn"))


if __name__ == "__main__":
    unittest.main()

"""Hero poses decoded from the ROM (skipped without SOM_ROM pointing at it)."""
import os
import unittest

from som_hero_gfx import HeroGfx, op_length, sext7

ROM = os.environ.get("SOM_ROM")


class FormatTest(unittest.TestCase):
    def test_operation_lengths_and_signed_coordinates(self):
        self.assertIsNone(op_length(0xFF))
        self.assertEqual((op_length(0xF8), op_length(0xF9), op_length(0xF0), op_length(0x8A)),
                         (4, 3, 3, 1))
        self.assertEqual((sext7(0x7F), sext7(0x3F)), (-1, 63))


@unittest.skipUnless(ROM and os.path.exists(ROM), "set SOM_ROM to the Turbo ROM")
class RomTest(unittest.TestCase):
    def test_boy_axe_swing_has_frames_with_four_piece_bodies(self):
        gfx = HeroGfx.load(ROM)
        steps = gfx.script("axe", 0, "side")
        self.assertGreaterEqual(len(steps), 3)
        for flags, frame, _ in steps:
            pieces = gfx.pieces("boy", "axe", frame, flags)
            self.assertTrue(1 <= len(pieces) <= 4)
            image = gfx.render("boy", pieces)
            self.assertGreater(sum(1 for row in image for value in row if value), 100)

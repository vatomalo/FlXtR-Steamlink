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

    def test_weapon_pieces_draw_the_sword_in_the_swing(self):
        gfx = HeroGfx.load(ROM)
        sword_row = 9                       # first sword (D0:1000 + 12 * row)
        tiles = gfx.weapon_tiles(sword_row)
        self.assertEqual(len(tiles), 64 * 32)
        drawn = 0
        for flags, frame, _ in gfx.script("sword", 0, "side"):
            pieces = gfx.weapon_pieces(sword_row, frame, flags)
            self.assertTrue(1 <= len(pieces) <= 10)
            for *_, indices in pieces:
                self.assertTrue(all(i is None or 0 <= i < 64 for i in indices))
            image = gfx.render("boy", pieces, source=tiles, base=16)
            values = [v for row in image for v in row if v]
            self.assertTrue(all(17 <= v <= 21 for v in values))   # colours 1-5
            drawn += len(values)
        self.assertGreater(drawn, 150)

    def test_hero_kinds_share_the_named_hero_graphics(self):
        gfx = HeroGfx.load(ROM)
        for kind, name in ((0x80, "boy"), (0x81, "girl"), (0x82, "sprite")):
            entry, named = gfx.actor(kind), gfx.actor(name)
            for key in ("lists", "pool", "palette"):
                self.assertEqual(entry[key], named[key], (name, key))
        self.assertEqual(gfx.pieces(0x80, "axe", 3), gfx.pieces("boy", "axe", 3))

    def test_villager_stands_and_walks_in_every_facing(self):
        gfx = HeroGfx.load(ROM)
        villager = 0xC0                       # Potos villager, D7:8000
        table = gfx.actor(villager)["ordinary"]
        self.assertEqual(gfx.actor(villager)["pool"], 0x178000)
        for anim in (0, 1):                   # stand, walk (docs/npcs.md section 4)
            for direction in ("up", "down", "side"):
                steps = gfx.script(None, anim, direction, attack=False, table=table)
                self.assertTrue(steps)
                for flags, frame, _ in steps:
                    pieces = gfx.pieces(villager, None, frame, flags)
                    self.assertTrue(1 <= len(pieces) <= 6)
                    image = gfx.render(villager, pieces)
                    filled = sum(1 for row in image for value in row if value)
                    self.assertGreater(filled, 250, (anim, direction, frame))

    def test_working_villager_wears_its_colours_and_keeps_the_axe(self):
        import som_villager_work as W
        gfx = HeroGfx.load(ROM)
        cmap = W.colour_map(gfx, 0xC0)
        self.assertEqual(set(cmap), set(range(1, 16)))
        frames = W.work_frames(gfx, 0xC0, "axe", 18)
        self.assertEqual(len(frames), len(gfx.script("axe", 0, "side")))
        for image, _ in frames:
            used = {p for row in image for p in row if p}
            self.assertTrue(used & set(range(17, 22)), "axe drawn in every step")
            edge = [image[0], image[-1], [r[0] for r in image], [r[-1] for r in image]]
            self.assertFalse(any(p for line in edge for p in line), "nothing clipped")

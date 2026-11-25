"""The core's art (src/som_art_gen.h) equals the Python tools', byte for byte.
Needs your own ROM: SOM_ROM_PATH, or build/ref/turbo.smc; skipped without.
SOM_ART_CC picks another compiler or flags (e.g. "clang -O2")."""
import os
import shlex
import subprocess
import tempfile
import unittest
from pathlib import Path

import som_cartwheel
import som_hero_gfx
import som_weapon_mask

ROOT = Path(__file__).resolve().parent.parent
ROM = Path(os.environ.get("SOM_ROM_PATH", ROOT / "build/ref/turbo.smc"))


class ArtGenTests(unittest.TestCase):
    def build(self, folder):
        tool = Path(folder) / "gen"
        subprocess.run(shlex.split(os.environ.get("SOM_ART_CC", "cc -Os")) +
                       ["-std=c99", "-Wall", "-Wextra", "-Werror",
                        str(ROOT / "tests/som_art_gen_tool.c"), "-o", str(tool), "-lm"], check=True)
        return tool

    @unittest.skipUnless(ROM.is_file(), "no ROM")
    def test_same_bytes_as_the_tools(self):
        gfx = som_hero_gfx.HeroGfx.load(ROM)
        with tempfile.TemporaryDirectory() as folder:
            tool = self.build(folder)
            masks, moves = Path(folder) / "masks", Path(folder) / "moves"
            subprocess.run([tool, ROM, masks, moves], check=True, stdout=subprocess.DEVNULL)
            self.assertEqual(masks.read_bytes(), som_weapon_mask.pack(gfx, kusarigama=True))
            self.assertEqual(moves.read_bytes(), som_cartwheel.pack(gfx))

    def test_garbage_is_refused_or_harmless(self):
        with tempfile.TemporaryDirectory() as folder:
            tool = self.build(folder)
            for data in (b"", b"\x00"*1000, b"\xff"*0x400000, bytes(0x400000)):
                rom = Path(folder) / "rom"
                rom.write_bytes(data)
                done = subprocess.run([tool, rom, rom.with_name("a"), rom.with_name("b")],
                                      stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=60)
                self.assertIn(done.returncode, (0, 1))


if __name__ == "__main__":
    unittest.main()

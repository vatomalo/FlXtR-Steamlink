"""Map capture: layout from a synthetic work RAM, PNG preview."""
import unittest
import zlib

import som_map_capture as cap
from som_memory import WRAM


def image(width=4, height=3, wall=(1, 1)):
    ram = bytearray(0x20000)
    ram[0xDC] = 133
    ram[0xC0:0xC2] = (width * 16).to_bytes(2, "little")
    ram[0xC2:0xC4] = (height * 16).to_bytes(2, "little")
    ram[0xE4], ram[0xE6] = 1, 0x0F
    ram[0x10000 + wall[1] * 128 + wall[0]] = 5          # tile id 5 on layer 1
    ram[0x1B800 + 5 * 4] = 3                           # terrain type 3: a wall
    return WRAM(bytes(ram))


class MapCaptureTest(unittest.TestCase):
    def test_layout_of_both_layers_and_a_wall(self):
        info = cap.describe(image(), None, [{"x": 2, "y": 0, "id": 0x80}])
        self.assertEqual((info["map_id"], info["size"]), (133, [4, 3]))
        self.assertEqual(info["layers"]["1"]["tiles"][1][1], 5)
        self.assertEqual(info["layers"]["1"]["kinds"][1][1], "blocked")
        self.assertEqual(info["layers"]["1"]["kinds"][0][0], "open")
        self.assertIn("05", info["layers"]["1"]["attributes"])
        self.assertTrue(cap.field(image()))

    def test_preview_is_a_valid_png(self):
        data = cap.preview(cap.describe(image(), None, []))
        self.assertTrue(data.startswith(b"\x89PNG\r\n\x1a\n"))
        idat = data[data.index(b"IDAT") + 4:data.index(b"IEND") - 8]
        width = (2 * 4 + 2) * cap.CELL
        self.assertEqual(len(zlib.decompress(idat)), 3 * cap.CELL * (1 + width * 3))


if __name__ == "__main__":
    unittest.main()

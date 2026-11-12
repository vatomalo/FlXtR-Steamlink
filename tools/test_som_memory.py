"""Unit tests: synthetic raw WRAM, no ROM, emulator, or network required."""
import tempfile
import unittest
from pathlib import Path

from som_memory import WRAM, ObservationHistory


def put16(data, address, value):
    offset = address - 0x7E0000
    data[offset:offset + 2] = value.to_bytes(2, "little")


class TestWRAM(unittest.TestCase):
    def test_requires_raw_wram_not_savestate(self):
        with self.assertRaises(ValueError):
            WRAM(b"savestate")

    def test_bounds(self):
        reader = WRAM(bytes(128 * 1024))
        with self.assertRaises(ValueError):
            reader.u8(0x7DFFFF)
        with self.assertRaises(ValueError):
            reader.u16(0x7FFFFF)

    def test_decodes_documented_fields(self):
        data = bytearray(128 * 1024)
        data[0x7E00ED - 0x7E0000] = 0x80
        data[0x7EE000 - 0x7E0000] = 1
        data[0x7EE180 - 0x7E0000] = 0x80
        data[0x7EE181 - 0x7E0000] = 12
        put16(data, 0x7EE182, 77)
        put16(data, 0x7EE184, 100)
        put16(data, 0x7EE602, 18)
        put16(data, 0x7EE604, 25)
        put16(data, 0x7EE782, 10)
        put16(data, 0x7EE784, 30)
        state = WRAM(data).observe()
        self.assertTrue(state["weapons_enabled"])
        self.assertEqual(state["party"][0]["graphic"], "boy")
        self.assertEqual(state["party"][0]["hp"], 77)
        self.assertEqual(state["enemy_slots"][0]["x_raw"], 25)
        self.assertIsNone(state["enemy_slots"][0]["active"])
        self.assertIsNone(state["player_position"])

    def test_history(self):
        with tempfile.TemporaryDirectory() as directory:
            history = ObservationHistory(Path(directory) / "history.sqlite")
            history.record({"player_position": None}, "right")
            self.assertEqual(history.recent()[0]["action"], "right")
            history.close()


if __name__ == "__main__":
    unittest.main()

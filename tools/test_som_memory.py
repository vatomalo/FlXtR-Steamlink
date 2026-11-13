"""Unit tests: synthetic raw WRAM, no ROM, emulator, or network required."""
import tempfile
import unittest
from pathlib import Path

from som_memory import WRAM, ObservationHistory


def put16(data, address, value):
    offset = address - 0x7E0000
    data[offset:offset + 2] = value.to_bytes(2, "little")


class TestWRAM(unittest.TestCase):
    def test_floor_height_collision_classification(self):
        self.assertEqual(WRAM.classify_tile([1, 0, 0, 0], 1)[0], "open")
        self.assertEqual(WRAM.classify_tile([2, 0, 0, 0], 1),
                         ("blocked", ["floor_height_wall"]))

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
        data[0x7EE02C - 0x7E0000] = 1
        data[0x7ECC73 - 0x7E0000] = 3
        data[0x7ECC7A - 0x7E0000] = 15
        data[0x7ECC7D - 0x7E0000] = 2
        put16(data, 0x7EE182, 77)
        put16(data, 0x7EE184, 100)
        put16(data, 0x7EE602, 18)
        put16(data, 0x7EE604, 25)
        data[0x7EE600 - 0x7E0000] = 1
        data[0x7EE780 - 0x7E0000] = 0x20
        put16(data, 0x7EE782, 10)
        put16(data, 0x7EE784, 30)
        state = WRAM(data).observe()
        self.assertTrue(state["weapons_enabled"])
        self.assertEqual(state["party"][0]["graphic"], "boy")
        self.assertEqual(state["party"][0]["hp"], 77)
        self.assertEqual(state["party"][0]["controller_binding"], 1)
        self.assertEqual(state["party"][0]["native_ai_behavior"], 3)
        self.assertEqual(state["party"][0]["native_ai_grid_cell"], 15)
        self.assertEqual(state["party"][0]["native_ai_charge_level"], 2)
        self.assertEqual(state["enemy_slots"][0]["x_px"], 18)
        self.assertEqual(state["enemy_slots"][0]["y_px"], 25)
        self.assertTrue(state["enemy_slots"][0]["active"])
        self.assertIsNone(state["player_position"])

    def test_runtime_verifies_and_classifies_nearby_tiles(self):
        data = bytearray(128 * 1024)
        data[0x7EE000 - 0x7E0000] = 1
        data[0x7E00E4 - 0x7E0000] = 1
        data[0x7E00E6 - 0x7E0000] = 0x0F
        put16(data, 0x7E00C0, 16 * 8)
        put16(data, 0x7E00C2, 16 * 8)
        put16(data, 0x7E00DC, 131)
        put16(data, 0x7EE002, 2 * 16 + 8)
        put16(data, 0x7EE004, 3 * 16 + 8)
        data[0x7EE00B - 0x7E0000] = 0
        data[0x7EE04C - 0x7E0000] = 5
        data[0x7EE048 - 0x7E0000] = 0
        data[0x7EE08E - 0x7E0000] = 6
        tile_map = 0x7F0000 - 0x7E0000
        data[tile_map:tile_map + 0x4000] = bytes([5]) * 0x4000
        data[tile_map + 3 * 128 + 3] = 6
        attributes = 0x7FB800 - 0x7E0000
        data[attributes + 5 * 4:attributes + 5 * 4 + 4] = bytes([0, 6, 0, 0])
        data[attributes + 6 * 4:attributes + 6 * 4 + 4] = bytes([3, 0, 0, 0])
        state = WRAM(data).observe()
        context = state["tile_context"]
        self.assertTrue(context["runtime_verified"])
        self.assertEqual(context["map_id"], 131)
        self.assertEqual(context["game_mode"], "field")
        self.assertEqual(context["actor_tile"], [2, 3])
        self.assertEqual(context["passability_rows"][3][3:5], "@#")
        self.assertEqual(state["player_position"]["x_px"], 40)

    def test_tile_context_accepts_stale_id_when_collision_fields_agree(self):
        data = bytearray(128 * 1024)
        data[0x7EE000 - 0x7E0000] = 1
        data[0x7E00E4 - 0x7E0000] = 1
        data[0x7E00E6 - 0x7E0000] = 0x0F
        put16(data, 0x7E00C0, 16 * 8)
        put16(data, 0x7E00C2, 16 * 8)
        data[0x7EE04C - 0x7E0000] = 99
        context = WRAM(data).tile_context()
        self.assertTrue(context["runtime_verified"])
        self.assertEqual(context["verification_warning"], "stale_actor_tile_id_cache")

    def test_tile_context_fails_closed_when_collision_fields_disagree(self):
        data = bytearray(128 * 1024)
        data[0x7EE000 - 0x7E0000] = 1
        data[0x7E00E4 - 0x7E0000] = 1
        data[0x7E00E6 - 0x7E0000] = 0x0F
        put16(data, 0x7E00C0, 16 * 8)
        put16(data, 0x7E00C2, 16 * 8)
        data[0x7EE08E - 0x7E0000] = 1
        context = WRAM(data).tile_context()
        self.assertFalse(context["runtime_verified"])
        self.assertEqual(context["verification_error"], "actor_tile_attributes_disagree")
        self.assertNotIn("passability_rows", context)

    def test_history(self):
        with tempfile.TemporaryDirectory() as directory:
            history = ObservationHistory(Path(directory) / "history.sqlite")
            history.record({"player_position": None}, "right")
            self.assertEqual(history.recent()[0]["action"], "right")
            history.close()


if __name__ == "__main__":
    unittest.main()

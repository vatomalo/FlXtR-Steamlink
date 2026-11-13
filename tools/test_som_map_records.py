"""Map object-record parsing on a synthetic ROM; no network or ROM needed."""
import unittest

from som_map_records import BANK_C8, TABLE, records


def synthetic_rom():
    rom = bytearray(0x100000)
    # Map pointers: maps 0-4 share 0x8000 (no header); map 5 has two records.
    pointers = [0x8000] * 5 + [0x8000, 0x8018] + [0x8018] * 400
    for index, pointer in enumerate(pointers[:400]):
        rom[TABLE + 2 * index] = pointer & 0xFF
        rom[TABLE + 2 * index + 1] = pointer >> 8
    base = BANK_C8 + 0x8000 + 8
    rom[base:base + 8] = bytes([0x12, 0x0F, 5, 7, 0x40, 0x05, 0, 0])     # monster id 5
    rom[base + 8:base + 16] = bytes([0x00, 0x0F, 9, 3, 0x00, 0x85, 1, 0])  # NPC 0x85
    return bytes(rom)


class TestRecords(unittest.TestCase):
    def test_parses_records_of_a_map(self):
        found = records(synthetic_rom(), 5)
        self.assertEqual([(r["kind"], r["id"], r["x"], r["y"]) for r in found],
                         [("monster", 5, 5, 7), ("npc/chest/switch", 0x85, 9, 3)])
        self.assertEqual(found[0]["offset"] + 5, BANK_C8 + 0x8000 + 8 + 5)
        self.assertEqual(found[0]["range"], (0, 15))

    def test_map_without_records(self):
        self.assertEqual(records(synthetic_rom(), 0), [])


if __name__ == "__main__":
    unittest.main()

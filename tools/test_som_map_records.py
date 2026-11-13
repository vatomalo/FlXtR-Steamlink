"""Map object-record parsing on a synthetic ROM; no network or ROM needed."""
import unittest

from som_map_records import BANK_C8, TABLE, RecordError, cross_check, records


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


    def test_relocated_turbo_tables_are_refused_per_map(self):
        rom = bytearray(synthetic_rom())
        # Turbo-like: map 6's pointer jumps back to a low, relocated region.
        rom[TABLE + 12], rom[TABLE + 13] = 0x00, 0x0D
        with self.assertRaises(RecordError):
            records(bytes(rom), 5)        # its end now lies before its start
        with self.assertRaises(RecordError):
            records(bytes(rom), 6)        # previous map's pointer is higher
        self.assertEqual(len(records(synthetic_rom(), 5)), 2)


class TestCrossCheck(unittest.TestCase):
    def test_needs_one_spawned_monster_with_a_record_id(self):
        found = records(synthetic_rom(), 5)                 # monster id 5
        self.assertEqual(cross_check(found, {5, 10}), ({5}, {10}))
        # Turbo map 129: only extra monsters seen, records unconfirmed.
        self.assertEqual(cross_check(found, {10}), (set(), {10}))


if __name__ == "__main__":
    unittest.main()

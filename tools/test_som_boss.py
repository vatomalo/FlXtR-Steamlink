import json
import tempfile
import unittest
from pathlib import Path

from som_boss import BossRecorder
from som_memory import WRAM

SIZE = 0x20000


def snapshot(identity=0x57, hp=300, max_hp=300, active=1, map_id=260):
    raw = bytearray(SIZE)
    raw[0x00DC], raw[0x00DD] = map_id & 0xFF, map_id >> 8
    base = 0xE600
    raw[base] = active
    raw[base + 0x180] = identity
    raw[base + 0x182:base + 0x184] = hp.to_bytes(2, "little")
    raw[base + 0x184:base + 0x186] = max_hp.to_bytes(2, "little")
    raw[base + 0x10] = 0xAB                       # marker inside the object
    return bytes(raw)


class BossRecorderTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.logs = []
        self.rec = BossRecorder(self.tmp.name, self.logs.append, clock=lambda: 0)

    def observe(self, raw):
        return self.rec.observe(raw, WRAM(raw))

    def test_records_boss_object_and_wram(self):
        raw = snapshot()
        self.assertEqual(self.observe(raw), [0x57])
        entry = json.loads((Path(self.tmp.name) / "index.json").read_text())["57"]
        self.assertTrue(entry["full_hp"])
        self.assertEqual(entry["map_id"], 260)
        obj = (Path(self.tmp.name) / entry["object_file"]).read_bytes()
        self.assertEqual(len(obj), 0x200)
        self.assertEqual(obj[0x10], 0xAB)
        self.assertEqual((Path(self.tmp.name) / entry["wram_file"]).read_bytes(), raw)

    def test_ignores_small_monsters_dead_and_inactive(self):
        self.assertEqual(self.observe(snapshot(max_hp=80, hp=80)), [])
        self.assertEqual(self.observe(snapshot(hp=0)), [])
        self.assertEqual(self.observe(snapshot(active=0)), [])

    def test_full_hp_capture_replaces_hurt_one_only(self):
        self.assertEqual(self.observe(snapshot(hp=150)), [0x57])
        self.assertEqual(self.observe(snapshot(hp=140)), [])
        self.assertEqual(self.observe(snapshot(hp=300)), [0x57])
        self.assertEqual(self.observe(snapshot(hp=300)), [])

    def test_index_survives_restart(self):
        self.observe(snapshot())
        again = BossRecorder(self.tmp.name, self.logs.append, clock=lambda: 0)
        self.assertEqual(again.observe(snapshot(), WRAM(snapshot())), [])


if __name__ == "__main__":
    unittest.main()

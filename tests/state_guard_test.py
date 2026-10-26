import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('state_guard', Path('tools/state_guard.py'))
guard = importlib.util.module_from_spec(spec)
spec.loader.exec_module(guard)


class StateGuardTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.rom = self.root / 'roms' / 'Game.nes'
        self.rom.parent.mkdir();self.rom.write_bytes(b'fixture')
        self.file = self.rom.with_suffix('.state3')
        self.patch = patch.object(guard, 'ROOT', self.root);self.patch.start()
        self.env = patch.dict(os.environ, {'FLXTR_RETROARCH_HOME': str(self.root / 'retroarch')});self.env.start()

    def tearDown(self):
        self.env.stop();self.patch.stop();self.temp.cleanup()

    def capture(self, data):
        self.file.write_bytes(data)
        with guard.locked(): guard.capture(self.file)
        return guard.record_for(self.file)

    def test_auto_and_numbered_slots_and_checksums(self):
        for suffix in ('.state', '.state1', '.state99', '.state.auto'):
            self.rom.with_suffix(suffix).write_bytes(suffix.encode())
        self.rom.with_suffix('.state1.png').write_bytes(b'thumbnail')
        self.assertEqual(guard.snapshot(guard.sources_for(self.rom)), 4)
        found = list(guard.versions(self.rom))
        self.assertEqual(len(found), 4)
        for token, source, value, _ in found:
            folder, _ = guard.record_for(source)
            blob = folder / (value['hash'] + '.state')
            self.assertEqual(blob.read_bytes(), source.read_bytes())
            self.assertEqual(guard.digest(blob), value['hash'])

    def test_rotating_shadows_and_suspect_truncation(self):
        folder, _ = self.capture(b'A' * 16000)
        _, record = self.capture(b'B' * 17000)
        self.assertEqual(len(list(folder.glob('*.state'))), 2)
        current = record['current']['hash']
        _, record = self.capture(b'X' * 4096)
        self.assertEqual(record['current']['hash'], current)
        self.assertEqual(record['suspect']['size'], 4096)
        self.assertEqual(self.file.read_bytes(), b'X' * 4096)
        self.file.chmod(0o640)
        token = guard.source_id(self.file) + 'c'
        with patch.object(guard, 'emulator_running', return_value=False): guard.recover(self.rom, token)
        self.assertEqual(self.file.read_bytes(), b'B' * 17000)
        self.assertEqual(self.file.stat().st_mode & 0o777, 0o640)
        _, record = guard.record_for(self.file)
        self.assertTrue(any(v['size'] == 4096 for v in record['displaced']))
        restored_blob = folder / (record['current']['hash'] + '.state')
        restored_blob.unlink()
        guard.snapshot([self.file], verify=True)
        self.assertEqual(restored_blob.read_bytes(), self.file.read_bytes())

    def test_corrupt_shadow_and_running_emulator_cannot_overwrite_primary(self):
        folder, record = self.capture(b'original' * 2000)
        blob = folder / (record['current']['hash'] + '.state')
        blob.write_bytes(b'corrupt')
        self.file.write_bytes(b'new primary')
        token = guard.source_id(self.file) + 'c'
        with patch.object(guard, 'emulator_running', return_value=False):
            with self.assertRaisesRegex(ValueError, 'checksum'): guard.recover(self.rom, token)
        with patch.object(guard, 'emulator_running', return_value=True):
            with self.assertRaisesRegex(ValueError, 'Exit the game'): guard.recover(self.rom, token)
        self.assertEqual(self.file.read_bytes(), b'new primary')

    def test_removed_primary_can_be_recovered_and_different_rom_is_excluded(self):
        self.capture(b'original')
        self.file.unlink()
        token = guard.source_id(self.file) + 'c'
        with patch.object(guard, 'emulator_running', return_value=False): guard.recover(self.rom, token)
        self.assertEqual(self.file.read_bytes(), b'original')
        self.assertEqual(list(guard.versions(self.rom.with_name('Other.nes'))), [])

    def test_copy_in_progress_keeps_previous_manifest(self):
        _, record = self.capture(b'original')
        before = dict(record)
        self.file.write_bytes(b'new state')
        stamp = guard.stamp(self.file)
        with patch.object(guard, 'stamp', side_effect=[stamp, [999, 1, 1, 1]]):
            with self.assertRaisesRegex(ValueError, 'changed during copy'): guard.capture(self.file)
        self.assertEqual(guard.record_for(self.file)[1], before)
        self.assertEqual(self.file.read_bytes(), b'new state')

    def test_initial_shadow_survives_multiple_overwrites(self):
        folder, first = self.capture(b'initial' * 2000)
        baseline = first['current']['hash']
        for byte in (b'B', b'C', b'D', b'E'): self.capture(byte * 16000)
        self.assertEqual((folder / (baseline + '.state')).read_bytes(), b'initial' * 2000)
        self.assertTrue(any(tag == 'baseline' for _, _, _, tag in guard.versions(self.rom)))

    def test_damaged_index_does_not_delete_existing_shadow_data(self):
        folder, record = self.capture(b'original')
        blob = folder / (record['current']['hash'] + '.state')
        (folder / 'record.json').write_text('{incomplete')
        self.file.write_bytes(b'new state')
        with self.assertRaisesRegex(ValueError, 'index is damaged'): guard.capture(self.file)
        self.assertEqual(blob.read_bytes(), b'original')
        self.assertEqual(self.file.read_bytes(), b'new state')

    def test_per_rom_slots_preserve_global_default(self):
        cfg = self.root / 'retroarch/.home/.config/retroarch/retroarch.cfg'
        cfg.parent.mkdir(parents=True);cfg.write_text('state_slot = "3"\n')
        a, b = '0123456789abcdef', '1111111111111111'
        self.assertEqual(guard.slot_for(a), 3)
        guard.set_slot(a, 1)
        self.assertEqual(guard.slot_for(a), 4)
        self.assertEqual(guard.slot_for(b), 3)
        guard.set_slot(a, 95)
        self.assertEqual(guard.slot_for(a), 99)
        guard.set_slot(a, 1)
        self.assertEqual(guard.slot_for(a), 0)
        self.assertEqual(cfg.read_text(), 'state_slot = "3"\n')

    def test_watcher_captures_active_save_and_final_exit(self):
        self.capture(b'A' * 16000)
        (self.root / 'state-session.json').write_text(json.dumps({'rom': str(self.rom)}))
        code = ('import time;from pathlib import Path;'
                'time.sleep(.2);p=Path(' + repr(str(self.file)) + ');p.write_bytes(b"B"*17000);'
                'time.sleep(2.2);p.write_bytes(b"C"*18000);time.sleep(.2)')
        child = subprocess.Popen([sys.executable, '-c', code])
        # Reap concurrently, just as greenlink.sh waits on its game process.
        import threading
        waiter = threading.Thread(target=child.wait);waiter.start()
        guard.watch(child.pid);waiter.join()
        folder, record = guard.record_for(self.file)
        self.assertEqual((folder / (record['current']['hash'] + '.state')).read_bytes(), b'C' * 18000)
        self.assertEqual((folder / (record['previous']['hash'] + '.state')).read_bytes(), b'B' * 17000)


if __name__ == '__main__': unittest.main()

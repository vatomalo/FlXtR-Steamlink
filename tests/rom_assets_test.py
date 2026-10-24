import importlib.util
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('rom_assets', Path('tools/rom_assets.py'))
assets = importlib.util.module_from_spec(spec)
spec.loader.exec_module(assets)
WORKER = Path('build/greenlink-catalog').resolve()
CHT = '''cheats = 2
cheat0_desc = "Infinite Lives"
cheat0_code = "SXIOPO"
cheat0_enable = true
cheat1_desc = "Memory cheat"
cheat1_handler = 1
cheat1_address = 1234
cheat1_memory_search_size = 3
cheat1_value = 99
cheat1_enable = true
video_driver = "injected"
'''


class RomAssetsTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.patch = patch.object(assets, 'ROOT', self.root)
        self.patch.start()
        self.rom = self.root / 'test.nes'
        self.rom.write_bytes(b'NES')
        self.key = assets.identity(str(self.rom))
        self.games = [{'title': 'Super Mario Bros. 3 (USA)', 'rom': str(self.rom), 'core': '/unused/fceumm_libretro.so'}]
        (self.root / 'games.json').write_text(json.dumps(self.games))

    def tearDown(self):
        self.patch.stop()
        self.temp.cleanup()

    def test_audio_is_per_rom_and_preserves_effects(self):
        assets.audio(self.key)
        self.assertIn('BACKGROUND MUSIC: OFF', (self.root / 'rom-assets.tsv').read_text())
        assets.audio(self.key, True)
        self.assertIn('BACKGROUND MUSIC: ON', (self.root / 'rom-assets.tsv').read_text())
        other = self.root / 'other.nes'
        self.games.append(dict(title='Other', rom=str(other), core='unused'))
        (self.root / 'games.json').write_text(json.dumps(self.games))
        assets.audio(assets.identity(str(other)))
        self.assertIn('BACKGROUND MUSIC: OFF', (self.root / 'rom-assets.tsv').read_text())
        self.assertEqual((self.root / 'rom-audio.tsv').read_text(), self.key + '\t1\n')
        self.assertFalse((self.root / 'game-runtime.cfg').exists())
        assets.audio(self.key, True)
        self.assertIn('BACKGROUND MUSIC: OFF', (self.root / 'rom-assets.tsv').read_text())

    def test_cover_borrows_art_without_modifying_game_and_resets(self):
        donor = '0123456789abcdef'
        index = [[donor, 'Another game', 'gameart:Nintendo - Nintendo Entertainment System|Another game']]
        with patch.object(assets, 'cover_index', return_value=index):
            original = (self.root / 'games.json').read_bytes()
            assets.covers(self.key, donor)
            self.assertIn(donor, (self.root / 'rom-covers.tsv').read_text())
            self.assertEqual((self.root / 'games.json').read_bytes(), original)
            assets.covers(self.key, 'automatic')
            self.assertEqual((self.root / 'rom-covers.tsv').read_text(), '')
            with self.assertRaises(ValueError): assets.covers(self.key, 'missing')

    def test_download_disabled_then_toggle_only_selected_rom_and_preserve_fields(self):
        choices = self.root / f'cheats/{self.key}.choices.json'
        assets.atomic(choices, json.dumps({'system': assets.SYSTEMS[0], 'files': ['Super Mario Bros. 3 (USA).cht']}))
        with patch.object(assets, 'fetch', return_value=CHT) as fetch:
            assets.install(self.key, '0')
            self.assertIn('Super%20Mario', fetch.call_args.args[0])
        path = self.root / f'cheats/{self.key}.cht'
        saved = path.read_text()
        self.assertNotIn('video_driver', saved)
        self.assertEqual(saved.count('_enable = false'), 2)
        self.assertIn('cheat1_address = 1234', saved)
        assets.cheats(self.key, '1')
        self.assertIn('cheat1_enable = true', path.read_text())
        self.assertIn('cheat0_enable = false', path.read_text())
        assets.cheats(self.key, '1')
        self.assertNotIn('_enable = true', path.read_text())
        with patch.object(assets, 'fetch', return_value='not a cheat file'):
            with self.assertRaises(ValueError): assets.install(self.key, '0')
        self.assertEqual(path.read_text(), saved)
        with patch.object(assets, 'fetch', return_value=CHT): assets.install(self.key, '0')
        self.assertEqual(path.with_suffix('.cht.previous').read_text(), saved)

    def test_search_regions_and_cached_large_system_tree(self):
        tree = {'tree': [{'type': 'blob', 'path': 'Super Mario Bros. 3 (USA).cht'},
                         {'type': 'blob', 'path': 'Super Mario Bros. 3 (Europe).cht'},
                         {'type': 'blob', 'path': '../bad.cht'}]}
        with patch.object(assets, 'fetch', side_effect=[json.dumps([{'name': assets.SYSTEMS[0], 'type': 'dir', 'sha': 'abc'}]), json.dumps(tree)]) as fetch:
            assets.search(self.key)
            assets.search(self.key)
            self.assertEqual(fetch.call_count, 2)
        choices = json.loads((self.root / f'cheats/{self.key}.choices.json').read_text())
        self.assertEqual(len(choices['files']), 2)
        self.assertTrue(all('Super Mario' in name for name in choices['files']))

    def test_system_and_validation(self):
        self.assertEqual(assets.system_for({'rom': '/mnt/disk/roms/PSPISO/Test.iso'}), 'Sony - PlayStation Portable')
        self.assertEqual(assets.system_for({'rom': '/mnt/disk/roms/PSXISO/Test.iso'}), 'Sony - PlayStation')
        with self.assertRaises(ValueError): assets.game('../bad')
        with self.assertRaises(ValueError): assets.parse_cheats('cheats = 9999999')
        with self.assertRaises(ValueError): assets.fetch('https://example.test/cheats')

    def test_catalog_applies_snapshot_to_stable_identity_and_export(self):
        core = self.root / 'fceumm_libretro.so'
        core.write_bytes(b'fixture')
        self.games[0]['core'] = str(core)
        (self.root / 'games.json').write_text(json.dumps(self.games))
        poster = 'gameart:Nintendo - Nintendo Entertainment System|Different Game'
        (self.root / 'rom-covers.tsv').write_text(f'{self.key}\t0123456789abcdef\tDonor\t{poster}\n')
        result = subprocess.run([str(WORKER), '--game-cover-index'], cwd=self.root, capture_output=True, text=True, check=True)
        self.assertIn(poster, result.stdout)
        self.games[0]['title'] = 'Renamed'
        (self.root / 'games.json').write_text(json.dumps(self.games))
        result = subprocess.run([str(WORKER), '--game-cover-index'], cwd=self.root, capture_output=True, text=True, check=True)
        self.assertIn(poster, result.stdout)
        self.assertIn('Renamed', result.stdout)


if __name__ == '__main__': unittest.main()

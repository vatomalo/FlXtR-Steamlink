"""Exercise the real launcher's console defaults, Y overrides and profiles."""
import json
import os
from pathlib import Path
import subprocess
import tempfile

worker = Path('build/greenlink-catalog').resolve()
with tempfile.TemporaryDirectory() as tmp:
    root = Path(tmp)
    home = root / 'retroarch'
    cores = home / 'cores'
    cores.mkdir(parents=True)
    config = home / '.home/.config/retroarch'
    config.mkdir(parents=True)
    (config / 'retroarch.cfg').write_text('# existing config\n')
    rom = root / 'test.nes'
    rom.write_bytes(b'fixture')
    for name in ('automatic', 'console', 'override'):
        (cores / f'{name}_libretro.so').write_bytes(b'fixture')
    runtime = home / 'retroarch.exec'
    runtime.write_text('#!/usr/bin/env python3\nimport json,sys,pathlib\npathlib.Path("argv.json").write_text(json.dumps(sys.argv[1:]))\n')
    runtime.chmod(0o755)
    (root / 'games.json').write_text(json.dumps([{'title': 'Test', 'rom': str(rom), 'core': str(cores / 'automatic_libretro.so')}]))
    env = {**os.environ, 'FLXTR_RETROARCH_HOME': str(home)}
    def launch(expected):
        (root / 'game-request').write_text('1 8 9 4 5\n')
        subprocess.run([str(worker), '--run-game'], cwd=root, env=env, check=True, capture_output=True)
        assert json.loads((home / 'argv.json').read_text())[-2] == str(cores / f'{expected}_libretro.so')
    launch('automatic')
    (root / 'platform-cores.tsv').write_text('0\tconsole\n')
    launch('console')
    key = 14695981039346656037
    for byte in str(rom).encode():
        key = ((key ^ byte) * 1099511628211) & ((1 << 64) - 1)
    (root / 'rom-cores.tsv').write_text(f'{key:016x}\toverride\n')
    launch('override')
    # Changing the console leaves this ROM's Y override intact.
    (root / 'platform-cores.tsv').write_text('0\tautomatic\n')
    launch('override')
    (root / 'platform-cores.tsv').write_text('0\tconsole\n')
    (root / 'rom-cores.tsv').unlink()
    launch('console')
    (root / 'platform-cores.tsv').write_text('0\tunavailable\n')
    launch('automatic')
    for profile, threaded, vsync in [(0, 'true', 'false'), (1, 'false', 'true'), (2, 'false', 'true')]:
        (root / 'settings.cfg').write_text(f'game_performance={profile}\n')
        launch('automatic')
        actual = (root / 'game-runtime.cfg').read_text()
        assert f'video_threaded = "{threaded}"' in actual
        assert f'video_vsync = "{vsync}"' in actual
        assert f'video_hard_sync = "{str(profile == 2).lower()}"' in actual
    assert (config / 'retroarch.cfg').read_text() == '# existing config\n'
    fixture = root / 'fixture.so'
    subprocess.run(['cc','-shared','-fPIC','-std=c99','tests/libretro_geometry_fixture.c','-o',str(fixture)],check=True)
    (cores / 'automatic_libretro.so').write_bytes(fixture.read_bytes())
    cheats = root / 'cheats'
    cheats.mkdir()
    chosen = cheats / f'{key:016x}.cht'
    chosen.write_text('cheats = 1\ncheat0_desc = "Test"\ncheat0_code = "SXIOPO"\ncheat0_enable = true\n')
    launch('automatic')
    actual = (root / 'game-runtime.cfg').read_text()
    expected = cheats / 'runtime' / f'{key:016x}'
    assert f'cheat_database_path = "{expected}"' in actual
    assert 'apply_cheats_after_load = "true"' in actual
    assert (expected / 'Fixture/test.cht').read_text() == chosen.read_text()
    chosen.write_text(chosen.read_text().replace('true','false'))
    launch('automatic')
    assert 'cheat0_enable = false' in (expected / 'Fixture/test.cht').read_text()
    (root / 'rom-state-slots.tsv').write_text(f'{key:016x}\t7\n')
    (root / 'tools').mkdir();(root / 'scripts').mkdir()
    (root / 'tools/state_guard.py').symlink_to(Path('tools/state_guard.py').resolve())
    (root / 'scripts/music-runtime.sh').write_text('#!/bin/sh\nexec python3 "$@"\n')
    state = root / 'test.state7';state.write_bytes(b'prelaunch snapshot')
    launch('automatic')
    actual = (root / 'game-runtime.cfg').read_text()
    assert 'state_slot = "7"' in actual
    assert 'input_load_state_btn = "4"' in actual and 'input_save_state_btn = "5"' in actual
    assert 'input_enable_hotkey_btn = "8"' in actual
    assert json.loads((root / 'state-session.json').read_text())['rom'] == str(rom)
    records = list((root / 'state-shadows').glob('*/record.json'))
    assert len(records) == 1
    meta = json.loads(records[0].read_text())
    assert (records[0].parent / (meta['current']['hash'] + '.state')).read_bytes() == state.read_bytes()
print('PASS: actual launches use Y ROM override > console default > automatic, and all three performance profiles')

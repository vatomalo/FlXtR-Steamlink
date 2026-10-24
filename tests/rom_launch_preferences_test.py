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
        (root / 'game-request').write_text('1 4 6\n')
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
print('PASS: actual launches use Y ROM override > console default > automatic, and all three performance profiles')

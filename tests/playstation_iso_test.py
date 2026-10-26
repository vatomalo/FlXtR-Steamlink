"""Scan real ISO paths, preserve PSP separation, and launch the selected disc."""
import json
import os
from pathlib import Path
import subprocess
import tempfile

worker = Path('build/greenlink-catalog').resolve()
with tempfile.TemporaryDirectory() as tmp:
    root = Path(tmp)
    home = root / 'retroarch'
    psx = home / 'roms/PlayStation'
    psp = home / 'roms/PSP'
    cores = home / 'cores'
    for directory in (psx, psp, cores):
        directory.mkdir(parents=True)
    for name in ('Disc.iso', 'Uppercase.ISO'):
        (psx / name).write_bytes(b'disc fixture')
    (psp / 'Portable.iso').write_bytes(b'disc fixture')
    (psx / 'Unsupported.iso.exe').write_bytes(b'fixture')
    env = dict(os.environ, FLXTR_RETROARCH_HOME=str(home), FLXTR_NO_ART='1')

    def scan():
        subprocess.run([str(worker), 'games', '1', '', '1', '0', '0'],
                       cwd=root, env=env, check=True, capture_output=True)
        return json.loads((root / 'games.json').read_text())

    assert scan() == [], 'An installed matching core is required'
    core = cores / 'pcsx_rearmed_libretro.so'
    core.write_bytes(b'core fixture')
    games = scan()
    assert {g['rom'] for g in games} == {str(psx / 'Disc.iso'), str(psx / 'Uppercase.ISO')}
    assert all(g['core'] == str(core) for g in games)
    folder = subprocess.run([str(worker), 'games-system', '1', '', '6', '0', '0'],
                            cwd=root, env=env, check=True, capture_output=True, text=True)
    assert 'total=2' in folder.stdout
    # Launch uses the imported absolute path and the PlayStation core.
    runtime = home / 'retroarch.exec'
    runtime.write_text('#!/usr/bin/env python3\nimport json,sys\nopen("argv.json","w").write(json.dumps(sys.argv[1:]))\n')
    runtime.chmod(0o755)
    (root / 'game-request').write_text('1 4 6\n')
    subprocess.run([str(worker), '--run-game'], cwd=root, env=env, check=True)
    args = json.loads((home / 'argv.json').read_text())
    assert args[-3:] == ['--libretro', str(core), games[0]['rom']]
    psp_core = cores / 'ppsspp_libretro.so'
    psp_core.write_bytes(b'core fixture')
    pspiso = home / 'roms/PSPISO'
    pspiso.mkdir()
    for name in ('Portable.ISO', 'Portable.pbp', 'Portable.cso'):
        (pspiso / name).write_bytes(b'PSP fixture')
    games = scan()
    assert sum(g['core'] == str(core) for g in games) == 2
    assert sum(g['core'] == str(psp_core) for g in games) == 4
    folder = subprocess.run([str(worker), 'games-system', '1', '', '7', '0', '0'],
                            cwd=root, env=env, check=True, capture_output=True, text=True)
    assert 'total=4' in folder.stdout
print('PASS: PSP ISO/PBP/CSO import and PSP folder routing with an installed PPSSPP core')
print('PASS: PlayStation .iso/.ISO import, matching core, PSP isolation and launch')

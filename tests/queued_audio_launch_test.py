"""Queued frontend opt-in, fail-closed behavior, config and runtime isolation."""
import json
import os
import pathlib
import subprocess
import tempfile

worker = pathlib.Path('build/greenlink-catalog').resolve()
with tempfile.TemporaryDirectory() as tmp:
    root = pathlib.Path(tmp)
    home = root / 'retro arch'
    home.mkdir()
    core = home / 'fceumm_libretro.so'
    core.write_bytes(b'fixture')
    rom = home / 'sound test.nes'
    rom.write_bytes(b'NES\x1a')
    (root / 'games.json').write_text(json.dumps([{'title': 'Test', 'core': str(core), 'rom': str(rom)}]))
    runtime = home / 'retroarch.exec'
    runtime.write_text('#!/usr/bin/env python3\nimport pathlib,os,json,sys\npathlib.Path("launch.json").write_text(json.dumps({"runtime":sys.argv[0],"env":dict(os.environ),"args":sys.argv[1:]}))\n')
    runtime.chmod(0o755)
    env = {**os.environ, 'FLXTR_RETROARCH_HOME': str(home), 'LD_LIBRARY_PATH': '/existing/path'}
    env.pop('FLXTR_USE_QUEUED_AUDIO', None)
    env.pop('FLXTR_USE_SYSTEM_SDL', None)
    def launch():
        (root / 'game-request').write_text('1 4 6\n')
        return subprocess.run([str(worker), '--run-game'], cwd=root, env=env, capture_output=True, text=True)
    assert launch().returncode == 0
    assert json.loads((home / 'launch.json').read_text())['runtime'] == str(runtime)
    assert 'audio_driver' not in (root / 'game-runtime.cfg').read_text()
    env['FLXTR_USE_QUEUED_AUDIO'] = '1'
    failed = launch()
    assert failed.returncode == 2 and 'frontend missing' in failed.stderr
    queued = root / 'retroarch-audio'
    queued.mkdir()
    alternate = queued / 'retroarch.exec'
    alternate.write_bytes(runtime.read_bytes())
    alternate.chmod(0o755)
    native = root / 'retroarch-libs'
    native.mkdir()
    (native / 'libSDL2-2.0.so.0').write_bytes(b'fixture')
    assert launch().returncode == 0
    result = json.loads((home / 'launch.json').read_text())
    assert result['runtime'] == str(alternate)
    assert result['env']['LD_LIBRARY_PATH'] == str(native) + ':/existing/path'
    assert result['args'][-3:] == ['--libretro', str(core), str(rom)]
    config = (root / 'game-runtime.cfg').read_text()
    assert 'audio_driver = "sdl2"' in config and 'audio_sync = "true"' in config
    assert env['LD_LIBRARY_PATH'] == '/existing/path'
    del env['FLXTR_USE_QUEUED_AUDIO']
    assert launch().returncode == 0
    assert json.loads((home / 'launch.json').read_text())['runtime'] == str(runtime)
print('PASS: queued frontend opt-in, missing binary, argv, config, private SDL, rollback')

"""Importer isolation, playlist compatibility, exact argv and cached paging."""
import json, os, pathlib, subprocess, tempfile
worker = pathlib.Path('build/greenlink-catalog').resolve()
with tempfile.TemporaryDirectory() as tmp:
    root = pathlib.Path(tmp)
    home = root / 'retro arch'
    (home / 'cores').mkdir(parents=True)
    cfg = home / '.home/.config/retroarch'
    cfg.mkdir(parents=True)
    (cfg / 'retroarch.cfg').write_text('video_driver = "gl"\n')
    roms = home / 'roms/NES'
    roms.mkdir(parents=True)
    core = home / 'cores/fceumm_libretro.so'
    core.write_bytes(b'fixture')
    for i in range(8): (roms / f'Game {i}.nes').write_bytes(b'NES\x1a')
    unsafe_name = roms / 'literal $(touch INJECTED); name.nes'
    unsafe_name.write_bytes(b'NES\x1a')
    (roms / 'unknown.zip').write_bytes(b'archive')
    (home / 'roms/unknown.zip').write_bytes(b'archive')  # ambiguous ZIP skipped
    (home / 'roms/missing.gba').write_bytes(b'fixture') # missing core skipped
    (cfg / 'content_history.lpl').write_text(json.dumps({'items':[{'path':str(unsafe_name),'label':'Exact title','core_path':str(core)}]}))
    (cfg / 'content_favorites.lpl').write_text(f'{roms / "Game 0.nes"}\nLegacy title\n{core}\nFCEUmm\n0|crc\nNES.lpl\n')
    env = {**os.environ, 'FLXTR_RETROARCH_HOME': str(home), 'FLXTR_NO_ART':'1'}
    def run(page=1, refresh=0):
        p = subprocess.run([str(worker),'games',str(page),'',str(refresh),'0','0'],cwd=root,env=env,capture_output=True,text=True,check=True)
        return p.stdout
    assert 'total=10' in run(refresh=1)
    games = json.loads((root / 'games.json').read_text())
    assert len({g['rom'] for g in games}) == 10
    assert 'total=10' in run(2)
    stamp=(root/'games.json').stat().st_mtime_ns
    run(); assert (root/'games.json').stat().st_mtime_ns == stamp
    index=next(i for i,g in enumerate(games,1) if g['rom']==str(unsafe_name))
    runtime=home/'retroarch.exec'
    runtime.write_text('#!/usr/bin/env python3\nimport json,sys,pathlib\npathlib.Path("argv.json").write_text(json.dumps(sys.argv[1:]))\n')
    runtime.chmod(0o755)
    (root/'game-request').write_text(f'{index} 4 6\n')
    subprocess.run([str(worker),'--run-game'],cwd=root,env=env,check=True)
    args=json.loads((home/'argv.json').read_text())
    assert args[-3:] == ['--libretro',str(core),str(unsafe_name)] and '--menu' not in args
    assert not (home/'INJECTED').exists() and not (root/'game-request').exists()
    assert 'input_enable_hotkey_btn = "4"' in (root/'game-runtime.cfg').read_text()
    (root/'game-request').write_text('99999 4 6\n')
    assert subprocess.run([str(worker),'--run-game'],cwd=root,env=env).returncode==2
print('PASS: JSON/legacy import, deduplication, bounded paging, unknown core exclusion, exact argv and exit hotkeys')

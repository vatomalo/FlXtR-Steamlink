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
    neo = home / 'roms/NeoGeo'
    neo.mkdir(parents=True)
    (neo / 'neogeo.zip').write_bytes(b'user-provided BIOS fixture')
    (neo / 'mslug.zip').write_bytes(b'ROM fixture')
    (home / 'cores/fbneo_libretro.so').write_bytes(b'core fixture')
    (home / 'roms/missing.gba').write_bytes(b'fixture') # missing core skipped
    (cfg / 'content_history.lpl').write_text(json.dumps({'items':[{'path':str(unsafe_name),'label':'Exact title','core_path':str(core)}]}))
    (cfg / 'content_favorites.lpl').write_text(f'{roms / "Game 0.nes"}\nLegacy title\n{core}\nFCEUmm\n0|crc\nNES.lpl\n')
    env = {**os.environ, 'FLXTR_RETROARCH_HOME': str(home), 'FLXTR_NO_ART':'1'}
    def run(page=1, refresh=0):
        p = subprocess.run([str(worker),'games',str(page),'',str(refresh),'0','0'],cwd=root,env=env,capture_output=True,text=True,check=True)
        return p.stdout
    assert 'total=11' in run(refresh=1)
    games = json.loads((root / 'games.json').read_text())
    assert len({g['rom'] for g in games}) == 11
    assert not any(g['rom'].endswith('/neogeo.zip') for g in games)
    assert any(g['rom'].endswith('/mslug.zip') and g['core'].endswith('fbneo_libretro.so') for g in games)
    assert 'total=11' in run(2)
    stamp=(root/'games.json').stat().st_mtime_ns
    run(); assert (root/'games.json').stat().st_mtime_ns == stamp
    index=next(i for i,g in enumerate(games,1) if g['rom']==str(unsafe_name))
    runtime=home/'retroarch.exec'
    runtime.write_text('#!/usr/bin/env python3\nimport json,sys,pathlib,os\npathlib.Path("runtime-env.json").write_text(json.dumps(dict(os.environ)))\npathlib.Path("argv.json").write_text(json.dumps(sys.argv[1:]))\n')
    runtime.chmod(0o755)
    (root/'game-request').write_text(f'{index} 4 6\n')
    subprocess.run([str(worker),'--run-game'],cwd=root,env=env,check=True)
    args=json.loads((home/'argv.json').read_text())
    assert args[-3:] == ['--libretro',str(core),str(unsafe_name)] and '--menu' not in args
    assert not (home/'INJECTED').exists() and not (root/'game-request').exists()
    assert 'input_enable_hotkey_btn = "4"' in (root/'game-runtime.cfg').read_text()
    # Bundled builds never replace system cores; explicit choices take priority.
    bundled=root/'cores';bundled.mkdir()
    fast=bundled/'snes9x2005_flxtr_libretro.so';fast.write_bytes(b'fixture')
    stock=home/'cores/snes9x2005_libretro.so';stock.write_bytes(b'fixture')
    snes=roms/'Test.sfc';snes.write_bytes(b'fixture')
    (root/'games.json').write_text(json.dumps([{'title':'Test','rom':str(snes),'core':str(stock)}]))
    def launch():
        (root/'game-request').write_text('1 4 6\n')
        subprocess.run([str(worker),'--run-game'],cwd=root,env=env,check=True)
        return json.loads((home/'argv.json').read_text())
    assert launch()[-2]==str(fast)
    assert 'video_fullscreen_x = "640"' in (root/'game-runtime.cfg').read_text()
    assert 'video_fullscreen_y = "480"' in (root/'game-runtime.cfg').read_text()
    for height,width in [(240,320),(720,1280),(1080,1920),(0,0),(480,640)]:
        (root/'settings.cfg').write_text(f'game_output={height}\n')
        launch()
        runtime_config=(root/'game-runtime.cfg').read_text()
        if height:
            assert f'video_fullscreen_x = "{width}"' in runtime_config
            assert f'video_fullscreen_y = "{height}"' in runtime_config
            assert 'video_windowed_fullscreen = "false"' in runtime_config
        else:
            assert 'video_fullscreen_x' not in runtime_config and 'video_fullscreen_y' not in runtime_config
    assert (cfg/'retroarch.cfg').read_text()=='video_driver = "gl"\n'
    assert 'video_shader_enable = "false"' in (root/'game-runtime.cfg').read_text()
    (root/'game-cores.tsv').write_text('1\tTest\tsnes9x2005\n')
    assert launch()[-2]==str(stock)
    (root/'game-cores.tsv').write_text('1\tTest\tsnes9x2005_flxtr\n')
    assert launch()[-2]==str(fast)
    (root/'game-cores.tsv').unlink()
    fast.unlink();assert launch()[-2]==str(stock)
    native=root/'retroarch-libs';native.mkdir()
    (native/'libSDL2-2.0.so.0').write_bytes(b'fixture')
    env['LD_LIBRARY_PATH']='/existing/search/path'
    launch()
    assert json.loads((home/'runtime-env.json').read_text())['LD_LIBRARY_PATH']==str(native)+':/existing/search/path'
    assert env['LD_LIBRARY_PATH']=='/existing/search/path'
    env['FLXTR_USE_SYSTEM_SDL']='1';launch()
    assert json.loads((home/'runtime-env.json').read_text())['LD_LIBRARY_PATH']=='/existing/search/path'
    del env['FLXTR_USE_SYSTEM_SDL']
    neo_core=home/'cores/fbalpha2012_neogeo_libretro.so';neo_core.write_bytes(b'fixture')
    (root/'games.json').write_text(json.dumps([{'title':'Neo','rom':str(neo/'mslug.zip'),'core':str(neo_core)}]))
    assert launch()[-2]==str(neo_core)
    assert 'video_shader_enable' not in (root/'game-runtime.cfg').read_text()
    (root/'game-request').write_text('99999 4 6\n')
    assert subprocess.run([str(worker),'--run-game'],cwd=root,env=env).returncode==2
print('PASS: JSON/legacy import, deduplication, bounded paging, unknown core exclusion, exact argv and exit hotkeys')

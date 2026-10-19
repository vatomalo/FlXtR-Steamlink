"""Desktop managed cores and installed RetroArch launch, without emulator code."""
from pathlib import Path
import argparse
import json
import os
import subprocess
import tempfile
parser=argparse.ArgumentParser()
parser.add_argument('--catalog',required=True,type=Path)
parser.add_argument('--retroarch',required=True,type=Path)
parser.add_argument('--tools',required=True,type=Path)
args=parser.parse_args()
worker=args.catalog.resolve();frontend=args.retroarch.resolve()
with tempfile.TemporaryDirectory(prefix='desktop games ø ') as directory:
    home=Path(directory)
    if os.name!='nt':(home/'tools').symlink_to(args.tools.resolve(),target_is_directory=True)
    cores=home/'cores';cores.mkdir()
    extension='.dll' if os.name=='nt' else '.so'
    core=cores/('fceumm_libretro'+extension);core.write_bytes(b'core fixture')
    roms=home/'roms';roms.mkdir()
    rom=roms/'literal $(not a shell); ø.nes';rom.write_bytes(b'NES fixture')
    env=dict(os.environ,FLXTR_RETROARCH=str(frontend),FLXTR_NO_ART='1')
    def run(*argv,check=True):
        result=subprocess.run([str(worker),*argv],cwd=home,env=env,capture_output=True,timeout=15)
        if check:assert result.returncode==0,(result.returncode,result.stdout,result.stderr)
        return result
    text=run('games-folders','1','','0','0','0','').stdout.decode()
    assert 'DOWNLOAD LIBRETRO CORES' in text and 'core-manager' in text
    text=run('core-list','1','','0','0','0','').stdout.decode()
    assert 'INSTALLED' in text and 'core-download' in text and 'fceumm' in text
    text=run('games','1','','1','0','0','').stdout.decode()
    assert 'literal' in text
    games=json.loads((home/'games.json').read_text(encoding='utf-8'))
    assert len(games)==1 and Path(games[0]['core'])==core
    (home/'game-request').write_text('1 -1 -1\n')
    run('--run-game')
    recorded=(home/'retroarch-args.txt').read_text(encoding='utf-8').splitlines()
    assert '-f' in recorded and '--appendconfig' in recorded
    assert recorded[-2:]==[str(core).replace('\\','/'),str(rom).replace('\\','/')]
    assert not (home/'INJECTED').exists()
    # An explicit missing frontend must not fall back to another installation.
    env['FLXTR_RETROARCH']=str(home/'missing-retroarch')
    (home/'game-request').write_text('1 -1 -1\n')
    result=run('--run-game',check=False)
    assert result.returncode==2 and 'INSTALL RETROARCH' in (home/'game-error.txt').read_text()
    if os.name!='nt':
        fake_bin=home/'fake-bin';fake_bin.mkdir()
        flatpak=fake_bin/'flatpak'
        flatpak.write_text('#!/bin/sh\nif [ "$1" = info ]; then exit 0; fi\nprintf "%s\\n" "$@" > flatpak-args.txt\n')
        flatpak.chmod(0o755)
        env.pop('FLXTR_RETROARCH')
        env['FLXTR_RETROARCH_HOME']=str(home/'no-retroarch')
        env['PATH']=str(fake_bin)
        (home/'game-request').write_text('1 -1 -1\n')
        run('--run-game')
        recorded=(home/'flatpak-args.txt').read_text().splitlines()
        assert recorded[0]=='run' and 'org.libretro.RetroArch' in recorded
        assert '--filesystem='+str(home) in recorded
        assert '--filesystem='+str(roms)+':ro' in recorded
print('PASS: managed desktop cores, core menu/helper, exact RetroArch argv and missing-frontend handling')

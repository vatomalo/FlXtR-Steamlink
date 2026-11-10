"""First startup repairs helper files omitted by legacy package updater allowlists."""
import os,shutil,subprocess,tempfile
from pathlib import Path
root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as directory:
 app=Path(directory);shutil.copy(root/'packaging/greenlink.sh',app/'greenlink.sh')
 runtime=app/'music-runtime/usr/lib/python3.12/site-packages';runtime.mkdir(parents=True)
 (app/'tools').mkdir();(app/'scripts').mkdir()
 (app/'scripts/music-runtime.sh').write_text('#!/bin/sh\nexit 0\n')
 (app/'greenlink').write_text('#!/bin/sh\nexit 0\n');(app/'greenlink').chmod(0o755)
 (app/'rom.nes').write_bytes(b'user ROM');(app/'tools/custom.py').write_text('user helper')
 for name in ('rom_assets','state_guard','rom_music','psp_core'):(runtime/f'{name}.py').write_text(f'# Packaged {name}\n')
 (app/'tools/rom_assets.py').write_text('# Old helper\n')
 env={**os.environ,'FLXTR_SKIP_UPDATE':'1'}
 subprocess.run(['sh',str(app/'greenlink.sh')],env=env,capture_output=True,text=True,check=True,timeout=5)
 for source in runtime.glob('*.py'):assert (app/'tools'/source.name).read_bytes()==source.read_bytes()
 stamp=(app/'tools/rom_assets.py').stat().st_mtime_ns
 subprocess.run(['sh',str(app/'greenlink.sh')],env=env,capture_output=True,text=True,check=True,timeout=5)
 assert (app/'tools/rom_assets.py').stat().st_mtime_ns==stamp
 assert (app/'rom.nes').read_bytes()==b'user ROM' and (app/'tools/custom.py').read_text()=='user helper'
print('PASS: legacy updater missing/stale ROM helpers repaired once; user ROMs and custom tools preserved')
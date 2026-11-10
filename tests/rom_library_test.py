"""ROM visibility, installed-core fallback, folder routing and persistent names."""
import json,os,subprocess,tempfile
from pathlib import Path
worker=Path('build/greenlink-catalog').resolve()
with tempfile.TemporaryDirectory() as directory:
 root=Path(directory);home=root/'retroarch';(home/'cores').mkdir(parents=True)
 (home/'cores/catsfc_libretro.so').write_bytes(b'core fixture')
 for folder,name in [('SNES','Demo.sfc'),('PSP','Demo.cso'),('GBA','Demo.gba'),('NES','Packed.zip')]:
  target=home/'roms'/folder/name;target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(b'ROM fixture')
 env={**os.environ,'FLXTR_RETROARCH_HOME':str(home),'FLXTR_NO_ART':'1'}
 def run(*args,good=True):
  result=subprocess.run([str(worker),*map(str,args)],cwd=root,env=env,text=True,capture_output=True)
  if good:assert result.returncode==0,result.stderr
  return result
 def page(kind,page=1,folder=0):return run(kind,page,'',folder,0,0).stdout
 rows=''.join(page('games-folders',n) for n in (1,2,3))
 downloads=[row.split('\t') for row in rows.splitlines() if row.startswith('DOWNLOADS\t')]
 assert len(downloads)==1 and downloads[0][4]=='rom-downloads' and downloads[0][5]=='11'
 assert 'PSP\t' in rows
 snes=page('games-system',folder=2)
 assert 'Demo\t' in snes and 'CORE MISSING' not in snes,snes
 packed=next(row.split('\t') for row in page('games-system',folder=1).splitlines() if '\tgame-zip\t' in row)
 assert packed[3].startswith('romid:');run('--rename-rom',packed[3][6:],'Renamed ZIP')
 assert 'Renamed ZIP' in page('games-system',folder=1)
 psp=page('games-system',folder=7)
 assert 'Demo\t' in psp and 'CORE MISSING' in psp,psp
 gba=page('games-system',folder=5);assert 'Demo\t' in gba and 'CORE MISSING' in gba
 games=json.loads((root/'games.json').read_text());assert len(games)==4
 game=next(g for g in games if g['rom'].endswith('.sfc'));assert game['core'].endswith('catsfc_libretro.so')
 identity=14695981039346656037
 for byte in game['rom'].encode():identity=((identity^byte)*1099511628211)&0xffffffffffffffff
 key=f'{identity:016x}';name="My SNES Game - What's New"
 run('--rename-rom',key,name)
 assert name in page('games-system',folder=2)
 run('games',1,'',1,0,0) # Reimport must preserve the user alias and exact ROM path.
 assert name in page('games-system',folder=2)
 assert Path(game['rom']).name=='Demo.sfc' and Path(game['rom']).read_bytes()==b'ROM fixture'
 assert json.loads((root/'rom-names.json').read_text())[key]==name
 outside=root/'custom.sfc';outside.write_bytes(b'custom explicit import');games=json.loads((root/'games.json').read_text());games.append({'title':'Custom import','rom':str(outside),'core':game['core']});(root/'games.json').write_text(json.dumps(games))
 (root/'games-import-version').unlink();run('games',1,'',1,0,0)
 assert any(g['rom']==str(outside) for g in json.loads((root/'games.json').read_text()))
 assert name in page('games-system',folder=2)
 for invalid in ('','   ','bad\nname','a'*80):assert run('--rename-rom',key,invalid,good=False).returncode!=0
 assert run('--rename-rom','0000000000000000','Unknown',good=False).returncode!=0
 assert run('--rename-rom','../bad','Unknown',good=False).returncode!=0
print('PASS: ROM download folder kind, PSP visibility, core fallback, missing-core ROMs and rename persistence/validation')

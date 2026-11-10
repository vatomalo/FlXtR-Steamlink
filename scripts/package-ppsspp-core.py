#!/usr/bin/env python3
"""Package the optional SDK-built PSP core and corresponding patched sources."""
import hashlib,json,os,subprocess,tarfile,tempfile
from pathlib import Path
root=Path(__file__).resolve().parents[1]
source=Path(os.environ['FLXTR_PSP_SOURCE']);core=root/'build/ppsspp/ppsspp_libretro.so'
output=root/'dist';output.mkdir(exist_ok=True)
package=output/'FlXtR-PPSSPP-SteamLink-armv7.tgz'
with tarfile.open(package,'w:gz') as archive:
 archive.add(core,arcname='cores/ppsspp_libretro.so')
 for path in sorted((source/'assets').rglob('*')):
  if path.is_file() and '.git' not in path.parts:archive.add(path,arcname='system/PPSSPP/'+path.relative_to(source/'assets').as_posix())
 for folder in (source,source/'ffmpeg'):
  for path in folder.iterdir():
   if path.is_file() and path.name.upper().startswith(('LICENSE','COPYING','COPYRIGHT','NOTICE','AUTHORS')):
    archive.add(path,arcname='licenses/ppsspp/'+('ffmpeg-' if folder.name=='ffmpeg' else '')+path.name)
 archive.add(root/'scripts/build-ppsspp-core.sh',arcname='licenses/ppsspp/build-ppsspp-core.sh')
assert package.stat().st_size<24*1024*1024
sha=hashlib.file_digest(package.open('rb'),'sha256').hexdigest()
package.with_suffix('.tgz.sha256').write_text(f'{sha}  {package.name}\n')
(root/'packaging/psp-core.json').write_text(json.dumps({'source':'7b4ddb426bbe9e287bb7f19b0cfaebb4ea0d41d8','sha256':sha,'url':'https://github.com/vatomalo/FlXtR-Steamlink/releases/download/steamlink-psp-7b4ddb4-v1/'+package.name},indent=2)+'\n')
subprocess.run(['tar','--exclude=.git','--exclude=*.o','--exclude=*.a','--exclude=*.so','--exclude=*.lib','--exclude=*.dll','--exclude=*.exe','--exclude=*.pdb','--exclude=build-steamlink','-czf',str(output/'PPSSPP-SteamLink-corresponding-source.tgz'),'-C',str(source),'.'],check=True)
print('Optional PSP package:',package.stat().st_size,'bytes; SHA256',sha)
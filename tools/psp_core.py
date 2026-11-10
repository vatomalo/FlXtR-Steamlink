"""Install the pinned optional Steam Link PSP core, outside the small app package."""
import hashlib,json,os,ssl,tarfile,tempfile,urllib.request
from pathlib import Path,PurePosixPath
MAX_ARCHIVE=24*1024*1024
MAX_UNPACKED=64*1024*1024

def install(root,payload,digest):
 root=Path(root).resolve()
 if len(payload)>MAX_ARCHIVE or hashlib.sha256(payload).hexdigest()!=digest:raise ValueError('PSP core checksum mismatch')
 with tempfile.TemporaryDirectory(prefix='.psp-core-',dir=root) as directory:
  stage=Path(directory);archive=stage/'core.tgz';archive.write_bytes(payload)
  with tarfile.open(archive) as package:
   files=[];total=0
   for member in package:
    path=PurePosixPath(member.name)
    if path.is_absolute() or '..' in path.parts or not path.parts:raise ValueError('Unsafe PSP archive path')
    allowed=member.name=='cores/ppsspp_libretro.so' or member.name.startswith('system/PPSSPP/') or member.name.startswith('licenses/ppsspp/')
    if member.isdir():continue
    if not member.isfile() or not allowed or member.size<0:raise ValueError('Unexpected PSP archive member')
    total+=member.size
    if total>MAX_UNPACKED or len(files)>=3000:raise ValueError('PSP archive exceeds bounds')
    target=stage/'files'/str(path);target.parent.mkdir(parents=True,exist_ok=True)
    with package.extractfile(member) as source,target.open('wb') as destination:
     remaining=member.size
     while remaining:
      chunk=source.read(min(65536,remaining))
      if not chunk:raise ValueError('Truncated PSP archive')
      destination.write(chunk);remaining-=len(chunk)
    files.append((member.name,target))
   core=next((path for name,path in files if name=='cores/ppsspp_libretro.so'),None)
   header=b''
   if core:
    with core.open('rb') as stream:header=stream.read(20)
   if len(header)!=20 or header[:6]!=b'\x7fELF\x01\x01' or int.from_bytes(header[18:20],'little')!=40:raise ValueError('Missing ARM PSP core')
   # Assets first, core last: a failed extraction never replaces a working core.
   for name,source in sorted(files,key=lambda item:item[0]=='cores/ppsspp_libretro.so'):
    target=(root/name).resolve()
    if not target.is_relative_to(root):raise ValueError('PSP destination escapes app directory')
    target.parent.mkdir(parents=True,exist_ok=True);os.chmod(source,0o755 if name.endswith('.so') else 0o644);os.replace(source,target)


def ensure(root):
 root=Path(root).resolve();manifest=json.loads((root/'assets/psp-core.json').read_text());core=root/'cores/ppsspp_libretro.so';stamp=root/'cores/ppsspp-installed.json'
 if core.is_file() and stamp.is_file():
  try:saved=json.loads(stamp.read_text())
  except (ValueError,OSError):saved={}
  if saved.get('sha256')==manifest['sha256'] and core.stat().st_size==saved.get('size'):return
 url=manifest['url']
 if not url.startswith('https://github.com/vatomalo/FlXtR-Steamlink/releases/download/steamlink-psp-'):raise ValueError('Untrusted PSP core URL')
 context=ssl.create_default_context(cafile=str(root/'certs/cacert.pem'))
 with urllib.request.urlopen(url,context=context,timeout=60) as response:payload=response.read(MAX_ARCHIVE+1)
 install(root,payload,manifest['sha256']);next_stamp=stamp.with_suffix('.next');next_stamp.write_text(json.dumps({'sha256':manifest['sha256'],'size':core.stat().st_size})+'\n');os.replace(next_stamp,stamp)
 (root/'games-transfer-dirty').touch()
 print('PPSSPP core and assets are ready')

if __name__=='__main__':ensure(Path.cwd())
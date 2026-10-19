#!/usr/bin/env python3
"""Download explicitly selected official desktop cores into FlXtR's data/cores."""
import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import platform
import ssl
import stat
import struct
import subprocess
import sys
import tempfile
import time
import urllib.parse
import urllib.request
import zipfile

CORES = [
    ('fceumm', 'NES / FCEUmm'),
    ('snes9x2005', 'SNES / Snes9x 2005'),
    ('snes9x', 'SNES / Snes9x'),
    ('gambatte', 'GAME BOY + COLOR / Gambatte'),
    ('mgba', 'GAME BOY ADVANCE / mGBA'),
    ('picodrive', 'MEGA DRIVE + SEGA CD / PicoDrive'),
    ('genesis_plus_gx', 'MEGA DRIVE + MASTER SYSTEM / Genesis Plus GX'),
    ('mednafen_psx', 'PLAYSTATION / Beetle PSX'),
    ('stella', 'ATARI 2600 / Stella'),
    ('fbneo', 'NEO GEO + ARCADE / FinalBurn Neo'),
    ('mame2003_plus', 'ARCADE / MAME 2003 Plus'),
    ('ppsspp', 'PSP / PPSSPP'),
    ('2048', '2048 / STANDALONE TEST GAME'),
]
MAX_ARCHIVE = 64 * 1024 * 1024
MAX_CORE = 256 * 1024 * 1024
HOST = 'buildbot.libretro.com'
PAGE_SIZE = 6

def target():
    machine = platform.machine().lower()
    if machine not in ('amd64', 'x86_64'):
        raise ValueError('Desktop downloader requires x86-64; Steam Link needs SDK-built cores')
    if os.name == 'nt':
        return 'windows', '.dll'
    if sys.platform.startswith('linux'):
        return 'linux', '.so'
    raise ValueError('Unsupported core platform')

def regular(path):
    try:
        info = path.lstat()
        return stat.S_ISREG(info.st_mode) and not getattr(info, 'st_file_attributes', 0) & 0x400
    except FileNotFoundError:
        return False

def safe_directory(path):
    path.mkdir(mode=0o700, exist_ok=True)
    info = path.lstat()
    if not stat.S_ISDIR(info.st_mode) or getattr(info, 'st_file_attributes', 0) & 0x400:
        raise ValueError('Core directory must be a real directory')

def compatible_header(path, system):
    with path.open('rb') as stream:
        data = stream.read(4096)
    if system == 'linux':
        return len(data) >= 20 and data[:6] == b'\x7fELF\x02\x01' and struct.unpack_from('<H',data,18)[0] == 62
    if len(data) < 64 or data[:2] != b'MZ':
        return False
    offset = struct.unpack_from('<I',data,60)[0]
    if offset > len(data)-26:
        return False
    return data[offset:offset+4] == b'PE\0\0' and struct.unpack_from('<H',data,offset+4)[0] == 0x8664 and struct.unpack_from('<H',data,offset+24)[0] == 0x20b

class OfficialRedirects(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, request, fp, code, message, headers, new_url):
        parsed = urllib.parse.urlsplit(new_url)
        if parsed.scheme != 'https' or parsed.hostname != HOST or parsed.username or parsed.password:
            raise ValueError('Refusing redirect outside official HTTPS buildbot')
        return super().redirect_request(request, fp, code, message, headers, new_url)

def download(url, stream):
    cert = Path(__file__).resolve().parent.parent/'certs/cacert.pem'
    context = ssl.create_default_context(cafile=str(cert) if cert.is_file() else None)
    opener = urllib.request.build_opener(OfficialRedirects(), urllib.request.HTTPSHandler(context=context))
    request = urllib.request.Request(url,headers={'User-Agent':'FlXtR-Core-Manager/1'})
    total=0
    began=time.monotonic()
    with opener.open(request,timeout=20) as response:
        if response.status != 200:
            raise ValueError('Core server did not return a successful response')
        if int(response.headers.get('Content-Length','0')) > MAX_ARCHIVE:
            raise ValueError('Core archive is too large')
        while True:
            block=response.read(65536)
            if not block:break
            total+=len(block)
            if total>MAX_ARCHIVE or time.monotonic()-began>120:
                raise ValueError('Core download exceeds size/time limit')
            stream.write(block)
    if not total:raise ValueError('Empty core download')
    stream.seek(0)

def unpack(archive, member, output):
    with zipfile.ZipFile(archive) as bundle:
        entries = bundle.infolist()
        # Official selected-core archives must contain one exact root member.
        if len(entries)!=1 or entries[0].filename!=member:
            raise ValueError('Unexpected core archive members')
        entry=entries[0]
        mode=(entry.external_attr>>16)&0xffff
        if stat.S_ISLNK(mode) or entry.flag_bits&1 or entry.file_size<=0 or entry.file_size>MAX_CORE:
            raise ValueError('Unsafe or oversized core archive')
        digest=hashlib.sha256()
        total=0
        with bundle.open(entry) as source:
            while True:
                block=source.read(65536)
                if not block:break
                total+=len(block)
                if total>MAX_CORE:raise ValueError('Inflated core exceeds limit')
                output.write(block);digest.update(block)
        if total!=entry.file_size:raise ValueError('Incomplete core archive')
        output.flush();os.fsync(output.fileno())
        return digest.hexdigest()

def probe(path):
    library=ctypes.CDLL(str(path.resolve()))
    function=library.retro_api_version
    function.restype=ctypes.c_uint
    if function()!=1:raise ValueError('Unsupported libretro API')
    print('libretro API 1')

def acquire_lock(directory):
    lock=directory/'.install.lock'
    flags=os.O_RDWR|os.O_CREAT|getattr(os,'O_NOFOLLOW',0)
    fd=os.open(lock,flags,0o600)
    if not regular(lock):os.close(fd);raise ValueError('Invalid core lock file')
    file=os.fdopen(fd,'r+b')
    if os.name=='nt':
        import msvcrt
        if os.fstat(fd).st_size==0:file.write(b'0');file.flush()
        file.seek(0)
        try:msvcrt.locking(fd,msvcrt.LK_NBLCK,1)
        except OSError:file.close();raise ValueError('Another core install is running')
    else:
        import fcntl
        try:fcntl.flock(fd,fcntl.LOCK_EX|fcntl.LOCK_NB)
        except OSError:file.close();raise ValueError('Another core install is running')
    return file

def install(name):
    system,extension=target()
    if name not in dict(CORES):raise ValueError('Unknown core')
    directory=Path('cores')
    safe_directory(directory)
    filename=name+'_libretro'+extension
    destination=directory/filename
    url=f'https://{HOST}/nightly/{system}/x86_64/latest/{filename}.zip'
    with acquire_lock(directory):
        if os.path.lexists(destination):
            if not regular(destination):raise ValueError('Existing core is not a regular file')
            # Preserve manually supplied cores and previous downloads.
            return 'Existing core kept'
        temp_name=None
        try:
            with tempfile.TemporaryFile('w+b',dir=directory) as archive:
                download(url,archive)
                with tempfile.NamedTemporaryFile('w+b',prefix='.'+name+'-',suffix=extension,dir=directory,delete=False) as output:
                    temp_name=Path(output.name)
                    digest=unpack(archive,filename,output)
            if not compatible_header(temp_name,system):raise ValueError('Core binary has the wrong architecture/format')
            check=subprocess.run([sys.executable,'-I',str(Path(__file__).resolve()),'--probe',str(temp_name.resolve())],
                                 capture_output=True,timeout=20,
                                 creationflags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0)
            if check.returncode:
                detail=check.stderr.decode('utf-8',errors='replace').strip().splitlines()
                raise ValueError('Core cannot load on this system: '+(detail[-1] if detail else 'missing dependencies'))
            # Publication never overwrites a core, including a concurrent writer.
            if os.name=='nt':os.rename(temp_name,destination)
            else:os.link(temp_name,destination)
            metadata={'core':name,'platform':system+'-x86_64','url':url,'sha256':digest,
                      'installed_at':int(time.time()),'api':1}
            info=directory/(filename+'.json')
            with tempfile.NamedTemporaryFile('w',encoding='utf-8',dir=directory,delete=False) as out:
                json.dump(metadata,out,indent=2);out.write('\n');meta_temp=Path(out.name)
            os.replace(meta_temp,info)
            Path('games.json').unlink(missing_ok=True)
            return 'Core installed'
        finally:
            if temp_name:temp_name.unlink(missing_ok=True)

def listing(page):
    _,extension=target()
    print(f'# pages={(len(CORES)+PAGE_SIZE-1)//PAGE_SIZE} total={len(CORES)}')
    for index,(name,title) in enumerate(CORES):
        if not (page-1)*PAGE_SIZE<=index<page*PAGE_SIZE:continue
        present=regular(Path('cores')/(name+'_libretro'+extension))
        meta='INSTALLED / A KEEPS EXISTING CORE' if present else 'A INSTALL / RETROARCH REQUIRED'
        print(f'{title}\t{meta}\t\t{name}\tcore-download\t{index+1}\t0\t0')

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--list',action='store_true')
    parser.add_argument('--install',choices=[name for name,_ in CORES])
    parser.add_argument('--page',type=int,default=1,choices=range(1,4))
    parser.add_argument('--probe',type=Path)
    args=parser.parse_args()
    if args.probe:probe(args.probe);return
    if args.install:
        Path('core-error.txt').unlink(missing_ok=True)
        install(args.install)
    if args.install or args.list:listing(args.page)
    else:parser.error('Choose --list or --install')
if __name__=='__main__':
    try:main()
    except Exception as error:
        message=str(error)
        print(message,file=sys.stderr)
        if '--probe' not in sys.argv:
            Path('core-error.txt').write_text(message[:300],encoding='utf-8')
        sys.exit(1)

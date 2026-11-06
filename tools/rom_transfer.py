#!/usr/bin/env python3
"""Single-ROM transfers. Paths and core/state commands never come from peers."""
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import tempfile
import zipfile
import concurrent.futures
import http.client
import ipaddress
import secrets
import socket
import struct
import subprocess
import time
import urllib.parse

MAX_ROM=64*1024*1024
SYSTEMS={'NES':('.nes','.unf','.unif'),'SNES':('.sfc','.smc'),'GB':('.gb',),'GBC':('.gbc',),'GBA':('.gba',)}
ALIASES={'NES':('nes',),'SNES':('snes','sfc'),'GB':('gb','gameboy'),'GBC':('gbc','gameboycolor'),'GBA':('gba','gameboyadvance')}

def sha256(path):
    value=hashlib.sha256()
    with Path(path).open('rb') as source:
        for block in iter(lambda:source.read(65536),b''):value.update(block)
    return value.hexdigest()

def metadata(value):
    if not isinstance(value,dict) or type(value.get('protocol')) is not int or value['protocol']!=1:raise ValueError('Update both FlXtR devices')
    system=value.get('system');name=value.get('name');size=value.get('size');checksum=value.get('sha256')
    if not isinstance(system,str) or system not in SYSTEMS:raise ValueError('ROM sending supports NES, SNES, GB, GBC and GBA')
    if not isinstance(name,str) or not 0<len(name.encode('utf-8'))<=180 or name.startswith('.') or any(ord(c)<32 or c in '/\\:<>"|?*' for c in name) or name[-1:] in (' ','.'):raise ValueError('Invalid ROM filename')
    if Path(name).suffix.lower() not in SYSTEMS[system]:raise ValueError('ROM extension does not match its system folder')
    if type(size) is not int or not 0<size<=MAX_ROM:raise ValueError('ROM exceeds the 64 MiB transfer limit')
    if not isinstance(checksum,str) or not re.fullmatch('[0-9a-f]{64}',checksum):raise ValueError('Invalid ROM checksum')
    return system,name,size,checksum

def receive_folder(root,system):
    root=Path(root)
    setting=root/'rom-receive-root.txt'
    if setting.is_file():
        base=Path(setting.read_text().strip())
        if not base.is_absolute() or base==Path(base.anchor):raise ValueError('Invalid configured ROM root')
        bases=[base]
    else:
        bases=[p for p in (Path('/mnt/disk/roms'),Path(os.environ.get('FLXTR_RETROARCH_HOME','/home/apps/retroarch'))/'roms',root/'roms') if p.is_dir()]
        if not bases:bases=[root/'roms']
    for base in bases:
        if not base.is_dir():continue
        for child in base.iterdir():
            key=re.sub('[^a-z0-9]','',child.name.lower())
            if key in ALIASES[system] and child.is_dir():
                if child.is_symlink():raise ValueError('ROM system folder must not be a symbolic link')
                return child.resolve()
    base=bases[0];base.mkdir(parents=True,exist_ok=True)
    folder=base/system
    if folder.is_symlink():raise ValueError('ROM system folder must not be a symbolic link')
    folder.mkdir(exist_ok=True)
    return folder.resolve()

def receive(archive,root):
    """Validate into a private temporary file, then commit without overwriting."""
    with zipfile.ZipFile(archive) as bundle:
        if sorted(bundle.namelist())!=['rom.bin','rom.json']:raise ValueError('Invalid ROM transfer archive')
        info=bundle.getinfo('rom.json')
        if info.file_size>8192:raise ValueError('ROM metadata too large')
        system,name,size,checksum=metadata(json.loads(bundle.read(info)))
        entry=bundle.getinfo('rom.bin')
        if entry.file_size!=size:raise ValueError('ROM size mismatch')
        folder=receive_folder(root,system)
        if shutil.disk_usage(folder).free<size*2+8*1024*1024:raise ValueError('Not enough ROM storage')
        destination=folder/name
        if destination.is_symlink():raise ValueError('Existing ROM is a symbolic link')
        if destination.exists():
            if not destination.is_file() or destination.stat().st_size!=size or sha256(destination)!=checksum:raise ValueError('A different ROM with this filename already exists and was kept')
            return dict(status='stored',system=system,name=name,path=str(destination),sha256=checksum,size=size,alreadyPresent=True)
        temporary=None;created=False;inode=None
        try:
            with tempfile.NamedTemporaryFile(prefix='.flxtr-rom-',dir=folder,delete=False) as output:
                temporary=Path(output.name);copied=0;digest=hashlib.sha256()
                with bundle.open(entry) as source:
                    for block in iter(lambda:source.read(65536),b''):
                        copied+=len(block)
                        if copied>size:raise ValueError('ROM size mismatch')
                        output.write(block);digest.update(block)
                if copied!=size or digest.hexdigest()!=checksum:raise ValueError('ROM checksum mismatch')
                output.flush();os.fsync(output.fileno())
            # O_EXCL works on USB FAT filesystems too. An existing file is never
            # replaced; only the verified temporary data is copied into our file.
            with destination.open('xb') as output,temporary.open('rb') as source:
                created=True;inode=os.fstat(output.fileno()).st_ino
                shutil.copyfileobj(source,output,65536);output.flush();os.fsync(output.fileno())
            (Path(root)/'games-transfer-dirty').write_text(checksum+'\n')
            return dict(status='stored',system=system,name=name,path=str(destination),sha256=checksum,size=size,alreadyPresent=False)
        except BaseException:
            if created:
                try:
                    if destination.stat().st_ino==inode:destination.unlink()
                except OSError:pass
            raise
        finally:
            if temporary:temporary.unlink(missing_ok=True)

def make_archive(source,destination,system=None):
    source=Path(source)
    if system is None:system=next((s for s,extensions in SYSTEMS.items() if source.suffix.lower() in extensions),'')
    value=dict(protocol=1,system=system,name=source.name,size=source.stat().st_size,sha256=sha256(source))
    metadata(value)
    with zipfile.ZipFile(destination,'w',compression=zipfile.ZIP_STORED) as bundle:
        bundle.writestr('rom.json',json.dumps(value));bundle.write(source,'rom.bin')
    return value

def private_ip(host,tail=False):
    try:
        ip=ipaddress.ip_address(host)
        return ip.version==4 and (ip in ipaddress.ip_network('100.64.0.0/10') if tail else any(ip in ipaddress.ip_network(n) for n in ('10.0.0.0/8','172.16.0.0/12','192.168.0.0/16','169.254.0.0/16')))
    except ValueError:return False

def endpoint(value,host=None):
    uri=urllib.parse.urlsplit(value)
    if uri.scheme!='http' or uri.username is not None or uri.password is not None or uri.query or uri.fragment or not re.fullmatch('/[0-9a-f]{32}/',uri.path) or not uri.port or not (private_ip(uri.hostname) or private_ip(uri.hostname,True)):raise ValueError('Invalid private FlXtR endpoint')
    if host is not None and uri.hostname!=host:raise ValueError('Device redirected to another host')
    if private_ip(uri.hostname,True) and uri.port!=43927:raise ValueError('Invalid FlXtR tailnet port')
    return uri

def read_exact(sock,size):
    result=b''
    while len(result)<size:
        block=sock.recv(size-len(result))
        if not block:raise OSError('Private Tailscale proxy closed')
        result+=block
    return result

class TailnetHTTP(http.client.HTTPConnection):
    def connect(self):
        self.sock=socket.create_connection(('127.0.0.1',43928),5);self.sock.settimeout(self.timeout)
        self.sock.sendall(b'\x05\x01\x00')
        if read_exact(self.sock,2)!=b'\x05\x00':raise OSError('Private Tailscale proxy unavailable')
        self.sock.sendall(b'\x05\x01\x00\x01'+socket.inet_aton(self.host)+struct.pack('!H',self.port))
        header=read_exact(self.sock,4)
        if header[:2]!=b'\x05\x00':raise OSError('Tailscale peer is unreachable')
        count={1:4,4:16}.get(header[3])
        if header[3]==3:count=read_exact(self.sock,1)[0]
        if count is None:raise OSError('Invalid Tailscale proxy response')
        read_exact(self.sock,count+2)

def request(address,route,upload=None):
    uri=endpoint(address)
    connection=(TailnetHTTP if private_ip(uri.hostname,True) else http.client.HTTPConnection)(uri.hostname,uri.port,timeout=300 if upload else 5)
    try:
        headers={};body=None
        if upload:body=Path(upload).open('rb');headers={'Content-Length':str(Path(upload).stat().st_size),'Content-Type':'application/octet-stream'}
        try:connection.request('POST' if upload else 'GET',uri.path+route,body,headers)
        finally:
            if body:body.close()
        response=connection.getresponse();raw=response.read(32769)
        if len(raw)>32768:raise ValueError('Device response is too large')
        value=json.loads(raw)
        if not 200<=response.status<300:raise ValueError(value.get('error','Device rejected ROM transfer'))
        return value
    finally:connection.close()

def discovered(value,host,tail):
    if value.get('app')!='FlXtR' or value.get('protocol')!=1 or 'rom-receiver' not in value.get('roles',[]):return None
    uri=endpoint(value.get('endpoint',''),host)
    if tail!=private_ip(host,True):return None
    identity=value.get('id',host)
    if not isinstance(identity,str) or not re.fullmatch('[a-zA-Z0-9._:-]{1,128}',identity):return None
    name=''.join(c for c in str(value.get('name','FlXtR')) if ord(c)>=32 and ord(c)!=127)[:80]
    return dict(id=identity,name=name or 'FlXtR',endpoint=uri.geturl(),transport='Tailscale' if tail else 'Local')

def discover(root):
    found={}
    def probe(peer):
        try:
            host=next(ip for ip in peer.get('TailscaleIPs',[]) if private_ip(ip,True))
            connection=TailnetHTTP(host,43927,timeout=4)
            try:
                connection.request('GET','/.well-known/flxtr');response=connection.getresponse();raw=response.read(32769)
                if response.status!=200 or len(raw)>32768:return None
                return discovered(json.loads(raw),host,True)
            finally:connection.close()
        except (ValueError,TypeError,AttributeError,OSError,StopIteration,http.client.HTTPException):return None
    try:
        cli=Path(root)/'tailscale/bin/tailscale';sock=Path(root)/'tailscale/tailscaled.sock'
        status=json.loads(subprocess.check_output([str(cli.resolve()),'--socket='+str(sock.resolve()),'status','--json'],timeout=8,stderr=subprocess.DEVNULL))
        peers=[p for p in status.get('Peer',{}).values() if p.get('Online')][:128]
    except (OSError,ValueError,subprocess.SubprocessError):peers=[]
    pool=concurrent.futures.ThreadPoolExecutor(max_workers=4);futures=[pool.submit(probe,p) for p in peers]
    try:
        nonce=secrets.token_hex(16);query=json.dumps(dict(app='FlXtR',protocol=1,op='discover',nonce=nonce)).encode()
        with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as udp:
            udp.setsockopt(socket.SOL_SOCKET,socket.SO_BROADCAST,1);udp.settimeout(.2);targets={'255.255.255.255'}
            try:
                for line in Path('/proc/net/arp').read_text().splitlines()[1:]:
                    parts=line.split()
                    if len(parts)>=4 and private_ip(parts[0]) and int(parts[2],16)&2:targets.add(parts[0])
            except (OSError,ValueError):pass
            for host in list(targets)[:65]:
                try:udp.sendto(query,(host,43929))
                except OSError:pass
            deadline=time.monotonic()+2
            while time.monotonic()<deadline:
                try:
                    raw,peer=udp.recvfrom(4096);value=json.loads(raw)
                    if value.get('nonce')!=nonce or not private_ip(peer[0]):continue
                    device=discovered(value,peer[0],False)
                    if device:found[device['id']]=device
                except (OSError,ValueError,TypeError,AttributeError):pass
        for future in concurrent.futures.as_completed(futures,timeout=8):
            device=future.result()
            if device:found[device['id']]=device # Prefer encrypted Tailscale when both routes exist.
    except concurrent.futures.TimeoutError:pass
    finally:pool.shutdown(wait=False,cancel_futures=True)
    return sorted(found.values(),key=lambda d:d['name'].lower())

def rom_key(path):
    value=14695981039346656037
    for byte in path.encode('utf-8'):value=((value^byte)*1099511628211)&0xffffffffffffffff
    return f'{value:016x}'

def main():
    import argparse,signal
    parser=argparse.ArgumentParser();parser.add_argument('command',choices=['devices','send']);parser.add_argument('key',nargs='?');parser.add_argument('device',nargs='?',type=int);args=parser.parse_args()
    root=Path.cwd();folder=root/'rom-transfer';folder.mkdir(mode=0o700,exist_ok=True)
    def interrupted(*_):raise KeyboardInterrupt()
    signal.signal(signal.SIGTERM,interrupted)
    try:
        if args.command=='devices':
            devices=discover(root)
            identity=(root/'handoff/device-id').read_text().strip() if (root/'handoff/device-id').is_file() else ''
            devices=[d for d in devices if d['id']!=identity][:16]
            (folder/'devices.json').write_text(json.dumps(devices));os.chmod(folder/'devices.json',0o600)
            (folder/'devices.tsv').write_text(''.join(d['name']+' / '+d['transport']+'\n' for d in devices))
            result=f'{len(devices)} ROM RECEIVER(S) FOUND'
        else:
            if not args.key or not re.fullmatch('[0-9a-fA-F]{16}',args.key):raise ValueError('Rescan games before sending this ROM')
            games=json.loads((root/'games.json').read_text());game=next((g for g in games[:2048] if rom_key(g.get('rom',''))==args.key.lower()),None)
            if game is None:raise ValueError('ROM is no longer in the library; rescan games')
            devices=json.loads((folder/'devices.json').read_text())
            if args.device is None or not 0<=args.device<len(devices):raise ValueError('Choose a receiving device')
            device=devices[args.device];cap=request(device['endpoint'],'capabilities')
            if cap.get('romTransfer')!=1:raise ValueError('Update FlXtR on the receiving device')
            with tempfile.TemporaryDirectory(prefix='send-',dir=folder) as temporary:
                bundle=Path(temporary)/'rom.zip';meta=make_archive(game['rom'],bundle)
                if meta['system'] not in cap.get('romSystems',[]) or meta['size']>cap.get('maxRomBytes',0):raise ValueError('This ROM exceeds the receiving device capabilities')
                receipt=request(device['endpoint'],'roms',bundle)
                if receipt.get('status')!='stored' or any(receipt.get(key)!=meta[key] for key in ('system','size','sha256')):raise ValueError('Receiver did not confirm the complete ROM')
                result=('ALREADY STORED' if receipt.get('alreadyPresent') else 'ROM STORED')+' / '+device['name']+' / '+meta['system']
        (folder/'result.txt').write_text(result+'\n');return 0
    except (Exception,KeyboardInterrupt) as error:
        text='TRANSFER STOPPED' if isinstance(error,KeyboardInterrupt) else str(error)
        (folder/'result.txt').write_text(''.join(c for c in text if ord(c)>=32)[:250]+'\n');return 1

if __name__=='__main__':raise SystemExit(main())

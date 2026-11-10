#!/usr/bin/env python3
"""Private userspace Tailscale instance for FlXtR TCP session handoff only."""
import hashlib
import ipaddress
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import tarfile
import time
import urllib.parse
import urllib.request

ROOT=Path('.')
PORT=43927

def folder(): return ROOT/'tailscale'
def write(name,value):
    path=folder()/name;temporary=path.with_suffix(path.suffix+'.next')
    temporary.write_text(value+'\n');os.chmod(temporary,0o600);os.replace(temporary,path)
def clear(name): (folder()/name).unlink(missing_ok=True)
def status(message): write('status.txt',message[:120])
def auth_url(value):
    parsed=urllib.parse.urlsplit(value)
    return value if parsed.scheme=='https' and parsed.hostname=='login.tailscale.com' and not parsed.username and not parsed.password and len(value)<500 else ''
def tail_ip(value):
    try:return ipaddress.ip_address(value) in ipaddress.ip_network('100.64.0.0/10')
    except ValueError:return False
def login_from_output(raw):
    decoder=json.JSONDecoder();remaining=raw.strip()
    while remaining:
        try:record,length=decoder.raw_decode(remaining)
        except ValueError:return ''
        if isinstance(record,dict):
            login=auth_url(record.get('AuthURL',''))
            if login:return login
        remaining=remaining[length:].lstrip()
    return ''
def install():
    config=json.loads((ROOT/'tailscale-arm.json').read_text())
    destination=folder()/'bin';marker=destination/'version'
    if marker.is_file() and marker.read_text().strip()==config['version'] and all((destination/name).is_file() for name in ('tailscale','tailscaled')): return
    status('INSTALLING VERIFIED TAILSCALE - PLEASE WAIT')
    if shutil.disk_usage(ROOT).free<256*1024*1024:raise ValueError('NOT ENOUGH SPACE: TAILSCALE NEEDS 256 MB FREE')
    archive=folder()/'download.tgz';temporary=folder()/'bin-new';temporary.mkdir(exist_ok=True)
    try:
        digest=hashlib.sha256();total=0
        request=urllib.request.Request(config['url'],headers={'User-Agent':'FlXtR-Tailscale-Installer/1'})
        with urllib.request.urlopen(request,timeout=20) as response,archive.open('wb') as out:
            if response.geturl()!=config['url']:raise ValueError('UNEXPECTED TAILSCALE DOWNLOAD REDIRECT')
            while True:
                block=response.read(65536)
                if not block:break
                total+=len(block)
                if total>config['maxDownloadBytes']:raise ValueError('TAILSCALE DOWNLOAD TOO LARGE')
                digest.update(block);out.write(block)
        if digest.hexdigest()!=config['sha256']:raise ValueError('TAILSCALE DOWNLOAD CHECKSUM FAILED')
        copied=set()
        with tarfile.open(archive,'r:gz') as bundle:
            for member in bundle:
                name=member.name.removeprefix('./')
                for binary in ('tailscale','tailscaled'):
                    if name!=config['directory']+'/'+binary:continue
                    if binary in copied or not member.isfile() or not 0<member.size<=128*1024*1024:raise ValueError('INVALID TAILSCALE EXECUTABLE')
                    target=temporary/binary
                    with bundle.extractfile(member) as source,target.open('wb') as out:shutil.copyfileobj(source,out,65536)
                    with target.open('rb') as source:elf=source.read(20)
                    if elf[:6]!=b'\x7fELF\x01\x01' or int.from_bytes(elf[18:20],'little')!=40:raise ValueError('TAILSCALE IS NOT AN ARM EXECUTABLE')
                    os.chmod(target,0o700);copied.add(binary)
        if copied!={'tailscale','tailscaled'}:raise ValueError('INCOMPLETE TAILSCALE PACKAGE')
        destination.mkdir(exist_ok=True)
        for binary in copied:os.replace(temporary/binary,destination/binary)
        marker.write_text(config['version']+'\n');os.chmod(marker,0o600)
    finally:
        archive.unlink(missing_ok=True)
        if temporary.is_dir():shutil.rmtree(temporary)

def cli(*arguments,timeout=8):
    result=subprocess.run([str((folder()/'bin/tailscale').resolve()),'--socket='+str((folder()/'tailscaled.sock').resolve()),*arguments],capture_output=True,text=True,timeout=timeout)
    return result.returncode,result.stdout

class Supervisor:
    def __init__(self):self.daemon=None;self.forward=None;self.stopping=False;self.login_at=0;self.retry_at=0;self.poll_at=0
    def stop(self):
        clear('ready');(ROOT/'handoff/remote-url.txt').unlink(missing_ok=True);clear('auth-url.txt');self.forward=None
        if self.daemon is not None:
            self.daemon.terminate()
            try:self.daemon.wait(timeout=5)
            except subprocess.TimeoutExpired:self.daemon.kill();self.daemon.wait()
            self.daemon=None
    def start(self):
        install()
        environment=dict(os.environ,GOMEMLIMIT='64MiB',GOGC='50')
        if (ROOT/'certs/cacert.pem').exists():environment['SSL_CERT_FILE']=str((ROOT/'certs/cacert.pem').resolve())
        self.daemon=subprocess.Popen([str((folder()/'bin/tailscaled').resolve()),'--tun=userspace-networking','--socks5-server=127.0.0.1:43928','--port=0','--socket='+str((folder()/'tailscaled.sock').resolve()),'--statedir='+str(folder().resolve()),'--state='+str((folder()/'tailscaled.state').resolve())],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,env=environment)
        status('CONNECTING TO TAILSCALE')
    def disconnect(self,logout=False):
        if self.daemon is not None and self.daemon.poll() is None:
            try:cli('logout' if logout else 'down')
            except (OSError,subprocess.SubprocessError):pass
        clear('enabled');self.stop()
        # Signing out while already disconnected must also forget this private
        # instance's cached identity; never touch the system Tailscale service.
        if logout:clear('tailscaled.state')
        status('SIGNED OUT' if logout else 'DISCONNECTED - PRESS A TO CONNECT')
    def tick(self):
        request=folder()/'request'
        if request.is_file():
            command=request.read_text().strip();request.unlink(missing_ok=True)
            if command in ('disconnect','logout'):self.disconnect(command=='logout');return
            if command=='connect':write('enabled','1');self.retry_at=0;self.login_at=0;self.poll_at=0
        if not (folder()/'enabled').exists():return
        if self.daemon is None or self.daemon.poll() is not None:
            self.stop()
            if time.monotonic()<self.retry_at:return
            self.retry_at=time.monotonic()+30;self.start();return
        if time.monotonic()<self.poll_at:return
        self.poll_at=time.monotonic()+5
        code,raw=cli('status','--json');state=json.loads(raw) if raw else {}
        login=auth_url(state.get('AuthURL',''))
        if login:write('auth-url.txt',login)
        backend=state.get('BackendState','')
        if backend!='Running':
            clear('ready');(ROOT/'handoff/remote-url.txt').unlink(missing_ok=True)
            if backend=='NeedsMachineAuth':clear('auth-url.txt');status('APPROVE THIS DEVICE IN YOUR TAILSCALE ADMIN CONSOLE');return
            if not login and time.monotonic()-self.login_at>15:
                self.login_at=time.monotonic()
                _,reply=cli('up','--json','--hostname=flxtr-steamlink','--accept-dns=false','--accept-routes=false','--timeout=1s',timeout=5)
                # CLI JSON can contain several newline-delimited progress records.
                login=login_from_output(reply)
                if login:write('auth-url.txt',login)
            status('SCAN QR TO SIGN IN TO TAILSCALE' if (folder()/'auth-url.txt').exists() else 'WAITING FOR TAILSCALE SIGN-IN')
            return
        clear('auth-url.txt')
        ip=next((value for value in state.get('TailscaleIPs',[]) if tail_ip(value)),None)
        if not ip:raise ValueError('TAILSCALE CONNECTED WITHOUT AN IPV4 ADDRESS')
        path=ROOT/'handoff/url.txt'
        if not path.exists():status('CONNECTED - WAITING FOR FLXTR RECEIVER');return
        local=urllib.parse.urlsplit(path.read_text().strip())
        if local.scheme!='http' or not local.port or len(local.path)!=34:raise ValueError('INVALID LOCAL RECEIVER ADDRESS')
        target='tcp://127.0.0.1:'+str(local.port)
        if self.forward!=target:
            code,_=cli('serve','--bg','--yes','--tcp='+str(PORT),target,timeout=12)
            if code:raise ValueError('TAILSCALE TCP FORWARDING WAS NOT ACCEPTED')
            self.forward=target
        remote='http://'+ip+':'+str(PORT)+local.path
        target_path=ROOT/'handoff/remote-url.txt';temporary=target_path.with_suffix('.next');temporary.write_text(remote+'\n');os.chmod(temporary,0o600);os.replace(temporary,target_path)
        write('ready','1');status('CONNECTED '+ip+' - REMOTE CONTINUE ON TV READY')
        self.poll_at=time.monotonic()+15
    def run(self):
        import fcntl
        folder().mkdir(mode=0o700,exist_ok=True);os.chmod(folder(),0o700)
        with (folder()/'.lock').open('a+') as lock:
            try:fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
            except BlockingIOError:return
            clear('ready');clear('auth-url.txt');(ROOT/'handoff/remote-url.txt').unlink(missing_ok=True)
            status('PRESS A TO INSTALL / CONNECT TAILSCALE')
            def stop_signal(*_):self.stopping=True
            signal.signal(signal.SIGTERM,stop_signal);signal.signal(signal.SIGINT,stop_signal)
            parent=os.getppid()
            try:
                while not self.stopping and os.getppid()==parent:
                    try:self.tick()
                    except (OSError,ValueError,subprocess.SubprocessError):
                        clear('ready');(ROOT/'handoff/remote-url.txt').unlink(missing_ok=True);self.forward=None
                        status('TAILSCALE NOT READY - CHECK NETWORK / RETRY WITH A')
                    time.sleep(2)
            finally:self.stop()

if __name__=='__main__':
    os.umask(0o077)
    Supervisor().run()

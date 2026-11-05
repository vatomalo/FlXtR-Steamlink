#!/usr/bin/env python3
"""Paired, bounded LAN session receiver. Runs beside the shell, never as root."""
import argparse
import hashlib
import http.server
import ipaddress
import json
import os
from pathlib import Path
import secrets
import shutil
import socket
import subprocess
import threading
import time
import urllib.parse
import zipfile

LIMIT = 128 * 1024 * 1024
SOURCE = 'gambatte:77f2567d718b8973d02caa2daa8dce4953faf811'
ROOT = Path('.')
LOCK = threading.Lock()

def native_environment():
    environment=dict(os.environ)
    previous=environment.get('FLXTR_HOST_LIBRARY_PATH',environment.get('LD_LIBRARY_PATH',''))
    environment['LD_LIBRARY_PATH']=str((ROOT/'retroarch-libs').resolve())+(':'+previous if previous else '')
    return environment

def atomic(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + '.next')
    temporary.write_text(json.dumps(value), encoding='utf-8')
    os.replace(temporary, path)

def digest(path):
    result = hashlib.sha256()
    with path.open('rb') as source:
        for block in iter(lambda: source.read(1024 * 1024), b''): result.update(block)
    return result.hexdigest()

def text(value, maximum=2048):
    if not isinstance(value, str) or len(value) > maximum or any(ord(c) < 32 for c in value):
        raise ValueError('Invalid session text')
    return value

def media_url(value):
    value = text(value,2047)
    parsed = urllib.parse.urlsplit(value)
    if not value.isascii() or parsed.scheme != 'https' or not parsed.hostname or parsed.username or parsed.password:
        raise ValueError('Only HTTPS video sources can be handed off')
    return value

def prepare(archive, directory):
    """Do not extract caller-selected paths or change existing ROMs/saves."""
    with zipfile.ZipFile(archive) as bundle:
        names = bundle.namelist()
        if len(names) != len(set(names)) or set(names) - {'session.json', 'state.bin'}:
            raise ValueError('Invalid session archive')
        info = bundle.getinfo('session.json')
        if info.file_size > 32768: raise ValueError('Session metadata too large')
        metadata = json.loads(bundle.read(info))
        if not isinstance(metadata,dict) or type(metadata.get('protocol')) is not int or metadata.get('protocol') != 1: raise ValueError('Update both FlXtR devices')
        kind = metadata.get('type')
        if kind == 'video':
            position = metadata.get('positionMs')
            if type(position) is not int or not 0 <= position <= 86400000: raise ValueError('Invalid video position')
            if metadata.get('audioUrl'): raise ValueError('Separate audio/video sources are not supported by this TV receiver')
            # Prefer resolving identity on the receiving device. Direct sources use
            # the supplied URL; no account keys or arbitrary HTTP headers cross devices.
            item = metadata.get('item', {})
            if not isinstance(item,dict): raise ValueError('Invalid video identity')
            url = media_url(metadata.get('url', ''))
            if item.get('kind') in ('movie', 'episode') and type(item.get('id')) is int and item['id'] > 0:
                args = ['./greenlink-catalog', 'source', '1', '', str(item['id']), str(int(item.get('season', 0))), str(int(item.get('episode', 0)))]
                try:
                    output = subprocess.check_output(args, timeout=70, stderr=subprocess.DEVNULL, text=True)
                    for row in output.splitlines():
                        fields = row.split('\t')
                        if len(fields)>4 and fields[4]=='server' and len(fields[3])<=64:
                            resolved=subprocess.check_output([args[0],'quality',*args[2:],fields[3]],timeout=50,stderr=subprocess.DEVNULL,text=True)
                            choices=[line.split('\t') for line in resolved.splitlines() if not line.startswith('#')]
                            selected=next((choice for choice in choices if len(choice)>4 and choice[3].startswith('https://')),None)
                            if selected: url=media_url(selected[3]);break
                except (OSError, subprocess.SubprocessError): pass
            subtitle = metadata.get('subtitle', '')
            if subtitle not in ('', 'en', 'no', 'es', 'tr'): raise ValueError('Unsupported subtitle preference')
            metadata.update(url=url, subtitle=subtitle, title=text(item.get('title', 'Continue on TV'), 79))
            caption=metadata.get('captionUrl','') if subtitle else ''
            if len(caption)>1023: raise ValueError('Subtitle link exceeds TV limit')
            metadata['captionUrl']=media_url(caption) if caption else ''
        elif kind == 'game':
            if metadata.get('source') != SOURCE: raise ValueError('No matching portable core build on this TV. Game Boy/Game Boy Color is supported first.')
            if type(metadata.get('quirks')) is not int or metadata['quirks']<0 or metadata['quirks'] & 125: raise ValueError('This core does not provide portable save states')
            rom_hash = metadata.get('romSha256', '')
            if not isinstance(rom_hash,str) or len(rom_hash) != 64 or any(c not in '0123456789abcdef' for c in rom_hash): raise ValueError('Invalid ROM fingerprint')
            state = bundle.getinfo('state.bin')
            if not 0 < state.file_size <= LIMIT: raise ValueError('Invalid save-state size')
            games = json.loads((ROOT / 'games.json').read_text())
            rom = None
            for game in games[:2048]:
                path = Path(game.get('rom', ''))
                if path.suffix.lower() not in ('.gb', '.gbc') or not path.is_file() or path.stat().st_size > 16*1024*1024: continue
                if digest(path) == rom_hash: rom = path.resolve(); break
            if rom is None: raise ValueError('Add and scan the identical ROM on the TV first')
            core = (ROOT / 'cores/gambatte_handoff_libretro.so').resolve()
            provenance = json.loads((ROOT / 'cores/gambatte-handoff.json').read_text())
            if provenance.get('source') != SOURCE or provenance.get('sha256') != digest(core): raise ValueError('TV core build cannot be verified')
            info = json.loads(subprocess.check_output(['./greenlink-handoff-game', '--info', str(core)], timeout=10,env=native_environment()))
            if info.get('coreInfo') != metadata.get('coreInfo'): raise ValueError('Core versions do not match')
            options = metadata.get('options', {})
            if not isinstance(options, dict) or len(options) > 256: raise ValueError('Invalid core settings')
            for key, value in options.items(): text(key, 160); text(value, 1024)
            target = directory / 'state.bin'
            with bundle.open(state) as source, target.open('wb') as output:
                copied = 0
                while True:
                    block = source.read(65536)
                    if not block: break
                    copied += len(block)
                    if copied > LIMIT: raise ValueError('State limit exceeded')
                    output.write(block)
            if digest(target) != metadata.get('stateSha256'): raise ValueError('Save-state checksum mismatch')
            metadata.update(rom=str(rom), core=str(core), state=str(target.resolve()), saveDirectory=str(directory.resolve()))
        else: raise ValueError('Unsupported session type')
    atomic(directory / 'session.json', metadata)
    return metadata

class Receiver(http.server.BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.0'
    def log_message(self, *args): pass # pairing tokens and video URLs stay out of logs
    def reply(self, code, body, kind='application/json'):
        data = body.encode() if isinstance(body, str) else json.dumps(body).encode()
        self.send_response(code); self.send_header('Content-Type', kind)
        self.send_header('Content-Length', str(len(data))); self.send_header('Cache-Control', 'no-store')
        self.send_header('Referrer-Policy', 'no-referrer'); self.end_headers(); self.wfile.write(data)
    def route(self):
        if not self.path.startswith(self.server.prefix): return None
        allowed={self.server.address}
        try:
            remote=urllib.parse.urlsplit((ROOT/'handoff/remote-url.txt').read_text().strip())
            if remote.scheme=='http' and remote.path==self.server.prefix:allowed.add(remote.netloc)
        except OSError:pass
        if self.headers.get('Host') not in allowed:return None
        origin = self.headers.get('Origin')
        if origin and origin not in {'http://'+address for address in allowed}: return None
        return self.path[len(self.server.prefix):]
    def armed(self):
        # Userspace Tailscale Serve connects to this receiver over loopback.
        # Private remote access is explicitly enabled by the Tailscale Connect
        # action; regular LAN clients still require the open receiver screen.
        if self.client_address[0]=='127.0.0.1' and (ROOT/'tailscale/ready').is_file():return True
        try: return 0 <= time.time() - (ROOT / 'handoff/armed').stat().st_mtime < 600
        except OSError: return False
    def do_GET(self):
        route = self.route()
        if route is None: return self.reply(403, {'error':'Pair again on the TV'})
        if route == '':
            address='http://'+self.headers['Host']+self.server.prefix
            link = 'flxtr://tv?url=' + urllib.parse.quote(address, safe='')
            return self.reply(200, '<!doctype html><meta name="viewport" content="width=device-width"><h1>Continue on TV</h1><p><a href="'+link+'">Pair with FlXtR on this phone</a></p><p>Or paste this page address into FlXtR’s Continue on TV menu.</p>', 'text/html; charset=utf-8')
        if route == 'capabilities': return self.reply(200, {'protocol':1, 'name':'Steam Link', 'armed':self.armed(), 'sources':[SOURCE], 'maxStateBytes':LIMIT})
        if route.startswith('status/'):
            identity = route[7:]
            if len(identity)!=32 or any(c not in '0123456789abcdef' for c in identity): return self.reply(400, {'error':'Invalid session'})
            directory = ROOT / 'handoff' / identity
            if (directory/'started').is_file(): return self.reply(200, {'status':'started'})
            if (directory/'failed').is_file(): return self.reply(200, {'status':'failed', 'error':(directory/'failed').read_text()[:300]})
            if (directory/'session.json').is_file(): return self.reply(200, {'status':'waiting'})
            return self.reply(404, {'error':'Unknown session'})
        self.reply(404, {'error':'Not found'})
    def do_POST(self):
        if self.route() != 'handoff': return self.reply(403, {'error':'Pair again on the TV'})
        if not self.armed(): return self.reply(409, {'error':'Open Settings > Continue on TV on the TV first'})
        if not LOCK.acquire(False): return self.reply(409, {'error':'Another transfer is active'})
        directory = None
        try:
            self.connection.settimeout(30)
            if self.headers.get('Transfer-Encoding'): raise ValueError('Chunked uploads are unsupported')
            length = int(self.headers.get('Content-Length', '0'))
            if not 0 < length <= LIMIT + 65536: raise ValueError('Transfer exceeds limit')
            if (ROOT/'handoff/pending').exists(): raise ValueError('TV is processing another session')
            if (ROOT/'handoff/busy').exists():raise ValueError('The TV is busy. Stop its current playback before sending another session.')
            if shutil.disk_usage(ROOT).free < length + 32*1024*1024: raise ValueError('Not enough free TV storage')
            identity = secrets.token_hex(16); directory = ROOT/'handoff'/identity; directory.mkdir(mode=0o700)
            archive = directory/'transfer.zip'
            with archive.open('wb') as output:
                left = length
                while left:
                    block = self.rfile.read(min(left,65536))
                    if not block: raise ValueError('Transfer interrupted')
                    output.write(block); left -= len(block)
            metadata = prepare(archive, directory); archive.unlink()
            # C shell consumes a bounded line containing only a server-created ID.
            if metadata['type']=='video':
                pending='video\t'+identity+'\t'+metadata['url']+'\t'+str(metadata['positionMs'])+'\t'+metadata['subtitle']+'\t'+metadata['title']+'\t'+metadata['captionUrl']+'\n'
            else: pending='game\t'+identity+'\n'
            (ROOT/'handoff/pending.next').write_text(pending)
            os.replace(ROOT/'handoff/pending.next', ROOT/'handoff/pending')
            self.reply(202, {'id':identity, 'status':'waiting', 'type':metadata['type']})
        except (ValueError, KeyError, OSError, RuntimeError, zipfile.BadZipFile, subprocess.SubprocessError) as error:
            if directory: shutil.rmtree(directory, ignore_errors=True)
            self.reply(400, {'error':str(error)[:300]})
        finally: LOCK.release()

def serve(ip=None):
    if not ip:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
            probe.connect(('192.0.2.1',9)); ip=probe.getsockname()[0]
    ipaddress.ip_address(ip)
    folder=ROOT/'handoff';folder.mkdir(mode=0o700,exist_ok=True)
    # Keep explicitly paired devices working after an app restart. The token is
    # private local state; neither it nor the tailnet identity is published.
    for name in ('armed','pending','launch','busy'): (folder/name).unlink(missing_ok=True)
    server=http.server.ThreadingHTTPServer(('0.0.0.0',0),Receiver)
    token_path=folder/'pairing-token'
    token=token_path.read_text().strip() if token_path.exists() else ''
    if len(token)!=32 or any(c not in '0123456789abcdef' for c in token):token=secrets.token_hex(16);token_path.write_text(token+'\n')
    os.chmod(token_path,0o600)
    server.prefix='/'+token+'/'
    server.address=ip+':'+str(server.server_port);server.url='http://'+server.address+server.prefix
    (folder/'url.txt').write_text(server.url+'\n')
    try: server.serve_forever()
    finally: server.server_close();(folder/'url.txt').unlink(missing_ok=True)

def launch(identity):
    if len(identity)!=32 or any(c not in '0123456789abcdef' for c in identity): return 2
    folder=ROOT/'handoff'/identity
    try:
        metadata=json.loads((folder/'session.json').read_text())
        if metadata['type']!='game': raise ValueError('Not a game session')
        returned=folder/'state-return.bin'
        if returned.is_file():
            metadata.update(state=str(returned.resolve()),stateSha256=digest(returned))
            atomic(folder/'session.json',metadata)
        (ROOT/'handoff/last').write_text(identity+'\n')
        (ROOT/'handoff/busy').touch()
        try:result=subprocess.call(['./greenlink-handoff-game',str((folder/'session.json').resolve())],env=native_environment())
        finally:(ROOT/'handoff/busy').unlink(missing_ok=True)
        if result and not (folder/'started').exists(): (folder/'failed').write_text('The TV could not restore this save state')
        return result
    except (OSError,ValueError,KeyError) as error: (folder/'failed').write_text(str(error));return 2

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('command',choices=['serve','launch']);parser.add_argument('value',nargs='?');args=parser.parse_args()
    raise SystemExit(serve(args.value) if args.command=='serve' else launch(args.value or ''))

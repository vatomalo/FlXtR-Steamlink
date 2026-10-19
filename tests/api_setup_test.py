"""Exercise the actual bounded HTTP setup listener and secret persistence."""
import http.client
import os
from pathlib import Path
import socket
import shutil
import json
import time
import subprocess
import tempfile
from urllib.parse import urlsplit, urlencode

root = Path(__file__).resolve().parent.parent
binary = Path(os.environ.get('FLXTR_API_TEST_BINARY', str(root / 'build/api-setup-test')))
if 'FLXTR_API_TEST_BINARY' not in os.environ:
    subprocess.run(['cc', '-std=c99', '-Wall', '-Wextra', '-Werror', str(root / 'tests/api_setup_server.c'), '-o', str(binary)], check=True)
subprocess.run([str(binary), 'expire'], check=True)
with tempfile.TemporaryDirectory(prefix='flxtr phone ø ') as directory:
    app = Path(directory)
    shutil.copytree(root / 'assets', app / 'assets')
    server = subprocess.Popen([str(binary)], cwd=app, stdout=subprocess.PIPE, text=True)
    try:
        url = urlsplit(server.stdout.readline().strip())
        assert len(url.path) == 34
        def request(method='GET', body=None, path=None, headers=None):
            conn = http.client.HTTPConnection(url.hostname, url.port, timeout=4)
            conn.request(method, path or url.path, body, headers or {})
            response = conn.getresponse()
            code, text = response.status, response.read().decode()
            assert response.getheader('Cache-Control') == 'no-store'
            assert response.getheader('Referrer-Policy') == 'same-origin'
            conn.close()
            return code, text
        assert request()[0] == 200
        assert request(path='/')[0] == 403
        assert request(headers={'Host': 'evil.example'})[0] == 403
        assert request(path=url.path+'phone')[0] == 200
        assert request(path=url.path+'app.js')[0] == 200
        headers = {'Content-Type': 'application/x-www-form-urlencoded', 'Origin': f'http://{url.netloc}'}
        assert request('POST','subdl=charset_test_key',headers=dict(headers,**{'Content-Type':'application/x-www-form-urlencoded;charset=UTF-8'}))[0]==200
        for field, filename, key in [('subdl', 'subdl-api-key.txt', 'test_SubDL-1234'), ('gamesdb', 'thegamesdb.key', 'test_GameDB-5678')]:
            code, text = request('POST', urlencode({field: key}), headers=headers)
            assert code == 200 and key not in text
            assert (app / filename).read_text() == key + '\n'
            if os.name != 'nt':
                assert (app / filename).stat().st_mode & 0o777 == 0o600
        original = (app / 'subdl-api-key.txt').read_bytes()
        for bad in ['subdl=short', 'subdl=bad%0Ainjection', 'subdl=bad%00injection', 'subdl=bad%GGencoding', 'subdl=' + 'a'*201, 'subdl=aaaaaaaa&gamesdb=bbbbbbbb', 'password=aaaaaaaa', '../key=aaaaaaaa']:
            assert request('POST', bad, headers=headers)[0] == 400, bad
            assert (app / 'subdl-api-key.txt').read_bytes() == original
        assert request('POST', 'subdl=aaaaaaaa', headers=dict(headers, Origin='https://evil.example'))[0] == 403
        assert request('POST', 'subdl=aaaaaaaa', headers={'Content-Type': 'text/plain'})[0] == 400
        assert request('POST', 'subdl=' + 'a'*600, headers=headers)[0] == 400
        # A request arriving in pieces must not save until its complete body arrives.
        conn = socket.create_connection((url.hostname, url.port), timeout=4)
        body = b'subdl=fragmented_key'
        conn.sendall(f'POST {url.path} HTTP/1.1\r\nHost: {url.netloc}\r\nContent-Type: application/x-www-form-urlencoded\r\nContent-Length: {len(body)}\r\n\r\n'.encode() + body[:8])
        assert (app / 'subdl-api-key.txt').read_bytes() == original
        conn.sendall(body[8:])
        response = b''
        while chunk := conn.recv(8192):
            response += chunk
        conn.close()
        assert b'200 OK' in response
        assert (app / 'subdl-api-key.txt').read_text() == 'fragmented_key\n'
        assert not list(app.glob('*.setup-*'))
        assert request('POST', 'subdl=aaaaaaaa', headers=dict(headers, Origin='null'))[0] == 403
        raw={'Content-Type':'application/octet-stream','Origin':f'http://{url.netloc}'}
        payload=os.urandom(1024*1024)
        assert request('POST',payload,path=url.path+'upload/test.mp4',headers=raw)[0]==200
        assert (app/'phone-files/test.mp4').read_bytes()==payload
        assert request('POST',b'new',path=url.path+'upload/test.mp4',headers=raw)[0]==409
        assert (app/'phone-files/test.mp4').read_bytes()==payload
        assert request('POST',b'bad',path=url.path+'upload/../evil.mp4',headers=raw)[0]==400
        assert request('POST',b'bad',path=url.path+'upload/run.sh',headers=raw)[0]==400
        for name in ('CON.mp4', 'nul.srt', 'COM9.extra.mp4'):
            assert request('POST', b'bad', path=url.path+'upload/'+name, headers=raw)[0] == 400
        assert request('POST',b'rom',path=url.path+'upload/test.nes',headers=raw)[0]==200
        assert (app/'roms/phone/test.nes').read_bytes()==b'rom'
        conn=socket.create_connection((url.hostname,url.port),timeout=4)
        conn.sendall(f'POST {url.path}upload/interrupted.mp4 HTTP/1.1\r\nHost: {url.netloc}\r\nContent-Type: application/octet-stream\r\nContent-Length: 100000\r\n\r\npartial'.encode())
        conn.close();time.sleep(.1)
        assert not (app/'phone-files/interrupted.mp4').exists()
        assert not list((app/'phone-files').glob('.upload-*'))
        for name in ('Alice','Bob'):
            code,token=request('POST',urlencode({'join':name}),headers=headers)
            assert code==200 and len(token)==32
        assert request('POST','party=start',headers=headers)[0]==200
        state=json.loads(request(path=url.path+'state')[1]);assert state['phase']==1 and len(state['players'])==2
        assert state['correct']==-1 and 'test.mp4' in state['files']
        answer=urlencode({'answer':f'{token}:0:1'})
        assert request('POST',answer,headers=headers)[0]==200
        assert request('POST',answer,headers=headers)[0]==409
    finally:
        server.terminate();server.wait(timeout=5)
print('PASS: setup HTTP, expiry, key storage permissions, fragmented input, invalid keys and cross-origin rejection')

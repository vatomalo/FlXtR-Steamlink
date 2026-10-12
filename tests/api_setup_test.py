"""Exercise the actual bounded HTTP setup listener and secret persistence."""
import http.client
import os
from pathlib import Path
import socket
import subprocess
import tempfile
from urllib.parse import urlsplit, urlencode

root = Path(__file__).resolve().parent.parent
binary = root / 'build/api-setup-test'
subprocess.run(['cc', '-std=c99', '-Wall', '-Wextra', '-Werror', str(root / 'tests/api_setup_server.c'), '-o', str(binary)], check=True)
subprocess.run([str(binary), 'expire'], check=True)
with tempfile.TemporaryDirectory() as directory:
    app = Path(directory)
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
            assert response.getheader('Referrer-Policy') == 'no-referrer'
            conn.close()
            return code, text
        assert request()[0] == 200
        assert request(path='/')[0] == 403
        assert request(headers={'Host': 'evil.example'})[0] == 403
        headers = {'Content-Type': 'application/x-www-form-urlencoded', 'Origin': f'http://{url.netloc}'}
        for field, filename, key in [('subdl', 'subdl-api-key.txt', 'test_SubDL-1234'), ('gamesdb', 'thegamesdb.key', 'test_GameDB-5678')]:
            code, text = request('POST', urlencode({field: key}), headers=headers)
            assert code == 200 and key not in text
            assert (app / filename).read_text() == key + '\n'
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
    finally:
        server.terminate();server.wait(timeout=5)
print('PASS: setup HTTP, expiry, key storage permissions, fragmented input, invalid keys and cross-origin rejection')

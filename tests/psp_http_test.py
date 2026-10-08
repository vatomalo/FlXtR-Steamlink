#!/usr/bin/env python3
"""Exercise the actual PSP HTTP/cache implementation on a native curl backend."""
import http.server
import subprocess
import threading

payload = bytes(i % 251 for i in range(700000))
class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *args): pass
    def do_GET(self):
        start, end = map(int, self.headers['Range'][6:].split('-'))
        end = min(end, len(payload) - 1)
        self.send_response(200 if self.path == '/no-range' else 206)
        if self.path != '/no-range':
            first = start + 1 if self.path == '/wrong-offset' else start
            self.send_header('Content-Range', f'bytes {first}-{end}/{len(payload)}')
        body = payload[start:end + 1]
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        self.wfile.write(body)

with http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler) as server:
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    for path, mode in [('/valid', 'valid'), ('/no-range', 'reject'), ('/wrong-offset', 'reject')]:
        subprocess.run(['build/psp-http-test', f'http://127.0.0.1:{server.server_port}{path}', mode], check=True)
    server.shutdown()

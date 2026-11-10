"""Exercise the native catalog and TV launcher against an authenticated companion."""
import http.server
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import threading
import unittest

ROOT = Path(__file__).resolve().parents[1]
TOKEN = 'a' * 64
GAME = {'id': '5E4132C860DED717ED0FEE86', 'title': 'Final Fantasy VII Remake'}


class GamezTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build = tempfile.TemporaryDirectory()
        cls.binary = Path(cls.build.name) / 'catalog'
        flags = subprocess.check_output(['pkg-config', '--cflags', '--libs', 'sdl2', 'SDL2_image', 'libcurl', 'json-c'], text=True).split()
        subprocess.run(['gcc', '-Os', '-std=c99', '-Wall', '-Wextra', '-Werror',
                        '-DFLXTR_VERSION="test"', str(ROOT / 'src/catalog.c'),
                        '-o', str(cls.binary), *flags, '-lcrypto'], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.build.cleanup()

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='approved games ')
        self.addCleanup(self.temp.cleanup)
        self.app = Path(self.temp.name)
        self.games = [GAME.copy()]
        self.requests = []
        self.authorized = True
        test = self

        class Handler(http.server.BaseHTTPRequestHandler):
            def do_GET(self):
                valid = self.headers.get('Authorization') == 'Bearer ' + TOKEN
                test.requests.append(('GET', self.path, valid))
                self.send_response(200 if valid and test.authorized else 401)
                self.end_headers()
                self.wfile.write(json.dumps({'games': test.games}).encode())

            def do_POST(self):
                valid = self.headers.get('Authorization') == 'Bearer ' + TOKEN
                game_id = self.rfile.read(int(self.headers['Content-Length'])).decode()
                test.requests.append(('POST', game_id, valid))
                accepted = valid and any(g['id'] == game_id for g in test.games)
                self.send_response(200 if accepted else 422)
                self.end_headers()
                self.wfile.write(b'{"status":"launched"}')

            def log_message(self, *_):
                pass

        self.server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
        thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        thread.start()
        self.addCleanup(self.server.server_close)
        self.addCleanup(self.server.shutdown)
        (self.app / 'token').write_text(TOKEN)
        self.host = '127.0.0.1:' + str(self.server.server_port)
        (self.app / 'gamez-bridge.conf').write_text(
            f"GAMEZ_HOST={self.host}\nGAMEZ_TOKEN_FILE='{self.app / 'token'}'\nGAMEZ_SUNSHINE_APP='FlXtR Gamez'\n")

    def catalog(self, page=1):
        return subprocess.run([str(self.binary), 'games-system', str(page), '', '11', '0', '0'],
                              cwd=self.app, env={**os.environ, 'FLXTR_NO_ART': '1'},
                              text=True, capture_output=True, timeout=12)

    def test_only_remote_approved_catalog_and_exact_id(self):
        result = self.catalog()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('total=1', result.stdout)
        self.assertIn('gamez:' + GAME['id'], result.stdout)
        self.assertNotIn('Ghost of Tsushima', result.stdout)
        self.assertEqual(self.requests, [('GET', '/v1/games', True)])

    def test_changes_are_live_including_revoked_approval(self):
        self.assertIn(GAME['title'], self.catalog().stdout)
        self.games = []
        self.assertEqual(self.catalog().stdout.strip(), '# pages=1 total=0')
        self.games = [{'id': 'ABC123', 'title': 'New approval'}]
        self.assertIn('gamez:ABC123', self.catalog().stdout)
        self.assertNotIn(GAME['title'], self.catalog().stdout)

    def test_empty_or_unconfigured_has_no_legacy_links(self):
        self.games = []
        self.assertEqual(self.catalog().stdout.strip(), '# pages=1 total=0')
        (self.app / 'gamez-bridge.conf').unlink()
        self.assertEqual(self.catalog().stdout.strip(), '# pages=1 total=0')

    def test_auth_failure_never_uses_old_catalog(self):
        self.catalog()
        self.authorized = False
        result = self.catalog()
        self.assertNotEqual(result.returncode, 0)
        self.assertNotIn(GAME['title'], result.stdout)

    def test_pagination_and_invalid_ids(self):
        self.games = [{'id': str(i), 'title': 'Approved ' + str(i)} for i in range(8)]
        self.games += [{'id': 'bad;command', 'title': 'Invalid'}, {'id': '0', 'title': 'Duplicate'}]
        self.games += [{'id': 'a' * 33, 'title': 'Too long'}]
        result = self.catalog(2)
        self.assertIn('pages=2 total=8', result.stdout)
        self.assertIn('gamez:6', result.stdout)
        self.assertNotIn('gamez:0', result.stdout)
        self.assertNotIn('Invalid', result.stdout)

    def launch(self, game_id):
        shutil.copy(ROOT / 'packaging/greenlink.sh', self.app)
        (self.app / '.no-auto-update').touch()
        ui = self.app / 'greenlink'
        ui.write_text('''#!/usr/bin/env python3
import os
from pathlib import Path
if not Path('ui-returned').exists():
    Path('ui-returned').touch()
    Path('gamez-request').write_text(os.environ['TEST_GAME_ID'])
    raise SystemExit(44)
''')
        ui.chmod(0o755)
        stream = self.app / 'moonlight-run.sh'
        stream.write_text('''#!/usr/bin/env python3
import json, os, sys
from pathlib import Path
Path('stream.json').write_text(json.dumps({'args':sys.argv[1:], 'managed':os.getenv('FLXTR_GAMEZ_MANAGED_SESSION')}))
''')
        # The real wrapper invokes this helper through sh.
        python_stream = self.app / 'fake-stream.py'
        stream.rename(python_stream)
        stream.write_text('#!/bin/sh\nexec python3 fake-stream.py "$@"\n')
        subprocess.run(['sh', './greenlink.sh'], cwd=self.app, timeout=15,
                       env={**os.environ, 'TEST_GAME_ID': game_id}, check=True,
                       capture_output=True)

    def test_real_wrapper_launches_full_id_through_managed_entry(self):
        self.launch(GAME['id'])
        self.assertIn(('POST', GAME['id'], True), self.requests)
        stream = json.loads((self.app / 'stream.json').read_text())
        self.assertEqual(stream, {'args': ['stream', '127.0.0.1', 'FlXtR Gamez'], 'managed': '1'})

    def test_revoked_game_does_not_start_moonlight(self):
        self.games = []
        self.launch(GAME['id'])
        self.assertFalse((self.app / 'stream.json').exists())


if __name__ == '__main__':
    unittest.main()

import http.client
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('build_portal', Path(__file__).resolve().parents[1] / 'tools/build_portal.py')
portal = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = portal
spec.loader.exec_module(portal)


class PortalTest(unittest.TestCase):
    def test_page_has_copy_output_button_with_http_fallback(self):
        page = (Path(__file__).resolve().parents[1] / 'web/skyloft/index.html').read_text()
        self.assertIn('id="copy-output"', page)
        self.assertIn("navigator.clipboard.writeText(value)", page)
        self.assertIn("document.execCommand('copy')", page)
        self.assertIn('id="clean-checkout"', page)
        self.assertIn("'/api/checkout/clean'", page)

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        root = Path(self.temp.name)
        self.source = root / 'source'
        self.source.mkdir()
        self.command = root / 'fake-build'
        self.command.write_text('''#!/usr/bin/env python3
import json,pathlib,sys,time
args=sys.argv[1:]
pathlib.Path('arguments.json').write_text(json.dumps(args))
if '--notes-file' in args:
    pathlib.Path('received-notes.md').write_text(pathlib.Path(args[args.index('--notes-file')+1]).read_text())
time.sleep(0.3)
out=pathlib.Path(args[args.index('--output-dir')+1]);out.mkdir()
name='FlXtR-Windows-x86_64.zip' if '--windows' in args else 'FlXtR-Android-preview.apk'
(out/name).write_bytes(b'test APK fixture')
print('Build complete.',flush=True)
''')
        self.command.chmod(0o755)
        self.config = portal.Config(self.source, root / 'state', str(self.command), {'127.0.0.1'})
        self.store = portal.Store(self.config)
        self.server = portal.ThreadingHTTPServer(('127.0.0.1', 0), portal.handler_for(self.store))
        self.server.daemon_threads = True
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()
        self.addCleanup(self.stop_server)

    def stop_server(self):
        self.server.shutdown()
        self.server.server_close()
        self.thread.join()

    def request(self, method, path, body=None, **headers):
        connection = http.client.HTTPConnection('127.0.0.1', self.server.server_port, timeout=5)
        try:
            connection.request(method, path, body=body, headers=headers)
            response = connection.getresponse()
            return response.status, response.read()
        finally:
            connection.close()

    def create(self, **values):
        payload = dict(platform='android', arch='arm64', branch='main', title='Phone build',
                       notes='- New menu\n- Literal $(touch never-execute)\n', publish=False)
        payload.update(values)
        status, response = self.request('POST', '/api/jobs', json.dumps(payload),
                                        **{'Content-Type': 'application/json', 'X-Flxtr-Request': '1'})
        return status, json.loads(response)

    def test_worker_survives_request_and_preserves_exact_notes(self):
        status, job = self.create()
        self.assertEqual(status, 202)
        for _ in range(100):
            job = self.store.read(self.store.folder(job['id']))
            if job['status'] not in ('queued', 'running'):
                break
            time.sleep(0.05)
        self.assertEqual(job['status'], 'succeeded', job)
        args = json.loads((self.source / 'arguments.json').read_text())
        self.assertIn('--no-publish', args)
        self.assertEqual(args[args.index('--arch') + 1], 'arm64')
        self.assertEqual((self.source / 'received-notes.md').read_text(), job['notes'])
        self.assertFalse((self.source / 'never-execute').exists())
        self.assertEqual(len(job['artifacts']), 1)
        code, data = self.request('GET', job['artifacts'][0]['url'])
        self.assertEqual((code, data), (200, b'test APK fixture'))
        code, log = self.request('GET', '/api/jobs/' + job['id'] + '/log')
        self.assertEqual(code, 200)
        self.assertIn('Build complete.', json.loads(log)['text'])

    def test_cross_origin_requests_are_rejected(self):
        status, _ = self.request('POST', '/api/jobs', '{}', **{
            'Content-Type': 'application/json', 'X-Flxtr-Request': '1', 'Origin': 'https://unrelated.example'})
        self.assertEqual(status, 403)
        status, _ = self.request('POST', '/api/jobs', '{}', **{'Content-Type': 'application/json'})
        self.assertEqual(status, 403)

    def test_unknown_host_and_path_are_rejected(self):
        status, _ = self.request('GET', '/api/state', Host='other.example')
        self.assertEqual(status, 403)
        status, _ = self.request('GET', '/api/jobs/../../etc/passwd')
        self.assertEqual(status, 404)

    def test_invalid_build_never_starts(self):
        for values in [dict(platform='macos'), dict(arch='x86'), dict(branch='main; echo bad'),
                       dict(branch='../main'), dict(platform='steamlink', arch='arm64'), dict(publish='yes')]:
            status, _ = self.create(**values)
            self.assertEqual(status, 400, values)
        self.assertEqual(self.store.list(), [])

    def test_windows_package_without_publication(self):
        status, job = self.create(platform='windows', arch='x86_64', publish=False)
        self.assertEqual(status, 202)
        for _ in range(100):
            job = self.store.read(self.store.folder(job['id']))
            if job['status'] not in ('queued', 'running'):
                break
            time.sleep(0.05)
        self.assertEqual(job['status'], 'succeeded', job)
        self.assertEqual(job['artifacts'][0]['name'], 'FlXtR-Windows-x86_64.zip')
        self.assertIn('--no-publish', json.loads((self.source / 'arguments.json').read_text()))

    def test_second_job_is_rejected(self):
        folder = self.store.jobs / ('a' * 32)
        folder.mkdir()
        portal.atomic_json(folder / 'job.json', dict(id='a' * 32, status='queued', created=time.time()))
        with self.assertRaises(BlockingIOError):
            self.store.create(dict(platform='android', arch='universal', branch='main'))

    def test_clean_checkout_preserves_changes_in_recovery_stash(self):
        subprocess.run(['git', 'init', '-q', '-b', 'main', str(self.source)], check=True)
        subprocess.run(['git', '-C', str(self.source), 'config', 'user.name', 'Portal Test'], check=True)
        subprocess.run(['git', '-C', str(self.source), 'config', 'user.email', 'portal@example.invalid'], check=True)
        tracked = self.source / 'tracked.txt'
        tracked.write_text('original\n')
        subprocess.run(['git', '-C', str(self.source), 'add', 'tracked.txt'], check=True)
        subprocess.run(['git', '-C', str(self.source), 'commit', '-qm', 'fixture'], check=True)
        tracked.write_text('changed\n')
        (self.source / 'untracked.txt').write_text('keep this too\n')
        generated = self.source / 'android/.gradle/cache.bin'
        generated.parent.mkdir(parents=True)
        generated.write_text('cache\n')
        status, response = self.request('POST', '/api/checkout/clean', '', **{'X-Flxtr-Request': '1'})
        result = json.loads(response)
        self.assertEqual(status, 200, result)
        self.assertEqual(result['stash'], 'stash@{0}')
        self.assertEqual(subprocess.check_output(
            ['git', '-C', str(self.source), 'status', '--porcelain'], text=True), '')
        stash = subprocess.check_output(
            ['git', '-C', str(self.source), 'stash', 'show', '--include-untracked', '--name-only', 'stash@{0}'], text=True)
        self.assertIn('tracked.txt', stash)
        self.assertIn('untracked.txt', stash)
        self.assertFalse(generated.exists())


if __name__ == '__main__':
    unittest.main()

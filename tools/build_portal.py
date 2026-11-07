#!/usr/bin/env python3
"""Small Tailscale-only build console. Apache serves the page; this serves /api/."""
import argparse
from dataclasses import dataclass
import fcntl
import hashlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import threading
import time
from urllib.parse import urlsplit
import uuid

PLATFORMS = {'android', 'steamlink', 'all'}
ARCHES = {'universal', 'arm64', 'armv7'}
ARTIFACTS = ('FlXtR-Android-preview.apk', 'greenlink.tgz')
JOB_ID = re.compile(r'^[0-9a-f]{32}$')


@dataclass
class Config:
    source: Path
    state: Path
    command: str
    hosts: set

    @classmethod
    def environment(cls):
        return cls(Path(os.environ.get('FLXTR_BUILD_SOURCE', '/opt/dockers/storage/projects/FlXtR/source')),
                   Path(os.environ.get('FLXTR_PORTAL_STATE', '/opt/dockers/storage/build-portal')),
                   os.environ.get('FLXTR_BUILD_COMMAND', '/usr/local/bin/flxtr-build'),
                   set(os.environ.get('FLXTR_PORTAL_HOSTS', '127.0.0.1,localhost,100.69.7.81,skyloft,skyloft.tailac13cd.ts.net').split(',')))


def validate(payload):
    if not isinstance(payload, dict):
        raise ValueError('Expected a build form.')
    platform = payload.get('platform', 'android')
    arch = payload.get('arch', 'universal')
    branch = payload.get('branch', 'main')
    title = payload.get('title', '')
    notes = payload.get('notes', '')
    publish = payload.get('publish', True)
    if platform not in PLATFORMS or arch not in ARCHES:
        raise ValueError('Choose a supported platform and architecture.')
    if platform == 'steamlink' and arch != 'armv7':
        raise ValueError('Steam Link uses ARMv7.')
    if not isinstance(branch, str) or not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9._/-]{0,199}', branch):
        raise ValueError('Enter a valid Git branch name.')
    if subprocess.run(['git', 'check-ref-format', 'refs/heads/' + branch], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode:
        raise ValueError('Enter a valid Git branch name.')
    if not isinstance(title, str) or len(title) > 160 or any(c in title for c in '\r\n\x00'):
        raise ValueError('Release title must be one line, at most 160 characters.')
    if not isinstance(notes, str) or len(notes.encode()) > 16384 or '\x00' in notes:
        raise ValueError('Release notes must be at most 16 KiB.')
    if type(publish) is not bool:
        raise ValueError('Publish must be on or off.')
    return dict(platform=platform, arch=arch, branch=branch, title=title.strip(), notes=notes, publish=publish)


def atomic_json(path, value):
    temporary = path.with_suffix('.next')
    temporary.write_text(json.dumps(value, ensure_ascii=False) + '\n')
    temporary.chmod(0o600)
    temporary.replace(path)


def process_key(pid):
    try:
        # starttime distinguishes a surviving worker from a reused process id.
        return Path(f'/proc/{pid}/stat').read_text().rsplit(')', 1)[1].split()[19]
    except (OSError, IndexError):
        return None


class Store:
    def __init__(self, config):
        self.config = config
        self.jobs = config.state / 'jobs'
        self.jobs.mkdir(parents=True, exist_ok=True, mode=0o700)
        self.guard = threading.Lock()
        self.cached_branches = (0, ['main'])

    def folder(self, identity):
        if not JOB_ID.fullmatch(identity):
            raise ValueError('Invalid build id.')
        folder = self.jobs / identity
        if not (folder / 'job.json').is_file():
            raise FileNotFoundError('Build not found.')
        return folder

    def read(self, folder):
        item = json.loads((folder / 'job.json').read_text())
        if item['status'] == 'running' and process_key(item.get('pid', 0)) != item.get('processKey'):
            item.update(status='failed', error='Build worker stopped unexpectedly.', finished=time.time())
            atomic_json(folder / 'job.json', item)
        if item['status'] == 'queued' and time.time() - item['created'] > 60:
            item.update(status='failed', error='Build worker did not start.', finished=time.time())
            atomic_json(folder / 'job.json', item)
        return item

    def list(self):
        items = []
        for folder in self.jobs.iterdir():
            if JOB_ID.fullmatch(folder.name) and (folder / 'job.json').exists():
                try:
                    items.append(self.read(folder))
                except (OSError, ValueError):
                    continue
        return sorted(items, key=lambda job: job['created'], reverse=True)[:30]

    def branches(self):
        if time.time() - self.cached_branches[0] > 60:
            try:
                result = subprocess.check_output(['git', '-C', str(self.config.source), 'ls-remote', '--heads', 'origin'], text=True, timeout=15)
                names = sorted({line.split('\trefs/heads/', 1)[1] for line in result.splitlines() if '\trefs/heads/' in line})
                self.cached_branches = (time.time(), sorted(names, key=lambda name: (name != 'main', name)))
            except (subprocess.SubprocessError, OSError):
                self.cached_branches = (time.time(), self.cached_branches[1])
        return self.cached_branches[1]

    def create(self, payload):
        build = validate(payload)
        with self.guard, (self.config.state / 'dispatch.lock').open('w') as lock:
            fcntl.flock(lock, fcntl.LOCK_EX)
            if any(job['status'] in ('queued', 'running') for job in self.list()):
                raise BlockingIOError('A build is already running. Wait for it to finish.')
            # CLI builds use this same lock, so do not queue work behind one.
            build_lock = Path(os.environ.get('FLXTR_STORAGE', '/opt/dockers/storage')) / 'build-home/flxtr-build.lock'
            if build_lock.exists():
                with build_lock.open('a') as current:
                    try:
                        fcntl.flock(current, fcntl.LOCK_EX | fcntl.LOCK_NB)
                    except BlockingIOError:
                        raise BlockingIOError('A command-line build is already running.') from None
            identity = uuid.uuid4().hex
            folder = self.jobs / identity
            folder.mkdir(mode=0o700)
            (folder / 'notes.md').write_text(build['notes'])
            (folder / 'notes.md').chmod(0o600)
            build.update(id=identity, status='queued', created=time.time(), artifacts=[], releases=[])
            atomic_json(folder / 'job.json', build)
            env = dict(os.environ, FLXTR_BUILD_SOURCE=str(self.config.source), FLXTR_PORTAL_STATE=str(self.config.state),
                       FLXTR_BUILD_COMMAND=self.config.command)
            with (folder / 'worker.log').open('ab') as output:
                worker = subprocess.Popen([sys.executable, str(Path(__file__).resolve()), '--job', identity], env=env,
                                          stdin=subprocess.DEVNULL, stdout=output, stderr=subprocess.STDOUT, start_new_session=True)
                threading.Thread(target=worker.wait, daemon=True).start()
            return build


def run_job(store, identity):
    folder = store.folder(identity)
    job = json.loads((folder / 'job.json').read_text())
    job.update(status='running', started=time.time(), pid=os.getpid(), processKey=process_key(os.getpid()))
    atomic_json(folder / 'job.json', job)
    command = [store.config.command, '--' + job['platform'], '--branch', job['branch'], '--arch', job['arch'],
               '--output-dir', str(folder / 'artifacts')]
    if not job['publish']:
        command.append('--no-publish')
    if job['title']:
        command.extend(['--release-title', job['title']])
    if job['notes'].strip():
        command.extend(['--notes-file', str(folder / 'notes.md')])
    try:
        with (folder / 'build.log').open('wb') as output:
            result = subprocess.run(command, cwd=store.config.source, stdin=subprocess.DEVNULL, stdout=output, stderr=subprocess.STDOUT)
        job.update(exitCode=result.returncode, status='succeeded' if result.returncode == 0 else 'failed')
        if result.returncode:
            job['error'] = 'Build or publishing failed. See the log below.'
        for name in ARTIFACTS:
            path = folder / 'artifacts' / name
            if path.is_file():
                with path.open('rb') as artifact:
                    digest = hashlib.file_digest(artifact, 'sha256').hexdigest()
                job['artifacts'].append(dict(name=name, size=path.stat().st_size, sha256=digest,
                                             url=f'/api/jobs/{identity}/artifacts/{name}'))
        text = (folder / 'build.log').read_text(errors='replace')
        job['releases'] = sorted(set(re.findall(r'https://github\.com/vatomalo/FlXtR-Steamlink/releases/tag/[A-Za-z0-9_.-]+', text)))
    except Exception as error:
        job.update(status='failed', error=str(error))
    job['finished'] = time.time()
    atomic_json(folder / 'job.json', job)


def handler_for(store):
    class Handler(BaseHTTPRequestHandler):
        server_version = 'FlXtR'

        def log_message(self, message, *args):
            print(message % args, flush=True)

        def reply(self, status, value):
            data = json.dumps(value, ensure_ascii=False).encode()
            self.send_response(status)
            self.send_header('Content-Type', 'application/json; charset=utf-8')
            self.send_header('Content-Length', str(len(data)))
            self.send_header('Cache-Control', 'no-store')
            self.send_header('X-Content-Type-Options', 'nosniff')
            self.end_headers()
            self.wfile.write(data)

        def valid_host(self):
            try:
                return urlsplit('//' + self.headers.get('Host', '')).hostname in store.config.hosts
            except ValueError:
                return False

        def do_GET(self):
            if not self.valid_host():
                return self.reply(403, {'error': 'Unknown host.'})
            path = urlsplit(self.path).path
            try:
                if path == '/api/config':
                    return self.reply(200, dict(branches=store.branches(), defaultBranch='main', server='SkyLoft'))
                if path == '/api/state':
                    return self.reply(200, {'jobs': store.list()})
                match = re.fullmatch(r'/api/jobs/([0-9a-f]{32})(?:/(log|artifacts/([^/]+)))?', path)
                if not match:
                    return self.reply(404, {'error': 'Not found.'})
                folder = store.folder(match[1])
                if match[2] == 'log':
                    log = folder / 'build.log'
                    text = ''
                    if log.exists():
                        with log.open('rb') as stream:
                            stream.seek(max(0, log.stat().st_size - 65536))
                            text = stream.read().decode(errors='replace')
                    text = re.sub(r'gh[opusr]_[A-Za-z0-9_]{20,}', '[redacted]', text)
                    return self.reply(200, {'text': text})
                if match[3]:
                    if match[3] not in ARTIFACTS:
                        return self.reply(404, {'error': 'Artifact not found.'})
                    artifact = folder / 'artifacts' / match[3]
                    if not artifact.is_file():
                        return self.reply(404, {'error': 'Artifact not found.'})
                    self.send_response(200)
                    self.send_header('Content-Type', 'application/octet-stream')
                    self.send_header('Content-Length', str(artifact.stat().st_size))
                    self.send_header('Content-Disposition', f'attachment; filename="{match[3]}"')
                    self.send_header('X-Content-Type-Options', 'nosniff')
                    self.end_headers()
                    with artifact.open('rb') as stream:
                        while chunk := stream.read(65536):
                            self.wfile.write(chunk)
                    return
                return self.reply(200, store.read(folder))
            except (FileNotFoundError, ValueError) as error:
                return self.reply(404, {'error': str(error)})
            except (BrokenPipeError, ConnectionResetError):
                pass

        def do_POST(self):
            if not self.valid_host():
                return self.reply(403, {'error': 'Unknown host.'})
            origin = self.headers.get('Origin')
            if (origin and origin not in ('http://' + self.headers['Host'], 'https://' + self.headers['Host'])) or self.headers.get('X-Flxtr-Request') != '1':
                return self.reply(403, {'error': 'Open the build page on SkyLoft to submit a build.'})
            if self.path != '/api/jobs':
                return self.reply(404, {'error': 'Not found.'})
            if self.headers.get('Content-Type', '').split(';', 1)[0] != 'application/json':
                return self.reply(415, {'error': 'Expected JSON.'})
            try:
                size = int(self.headers.get('Content-Length', '0'))
                if not 0 < size <= 32768:
                    return self.reply(413, {'error': 'Build form is too large.'})
                payload = json.loads(self.rfile.read(size))
                return self.reply(202, store.create(payload))
            except BlockingIOError as error:
                return self.reply(409, {'error': str(error)})
            except (ValueError, TypeError) as error:
                return self.reply(400, {'error': str(error)})

    return Handler


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--job')
    parser.add_argument('--port', type=int, default=8787)
    args = parser.parse_args()
    os.umask(0o077)
    store = Store(Config.environment())
    if args.job:
        run_job(store, args.job)
    else:
        server = ThreadingHTTPServer(('127.0.0.1', args.port), handler_for(store))
        server.daemon_threads = True
        print(f'FlXtR build API listening on 127.0.0.1:{args.port}', flush=True)
        server.serve_forever()


if __name__ == '__main__':
    main()

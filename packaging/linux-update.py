#!/usr/bin/env python3
"""Platform-specific, monotonic GitHub updates with atomic directory switching."""
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import tarfile
import tempfile
import urllib.request

BASE = 'https://github.com/vatomalo/FlXtR-Steamlink/releases/download/'
LIMIT = 64 * 1024 * 1024

def fetch(url, limit):
    if not url.startswith(BASE):
        raise ValueError('Unexpected release URL')
    with urllib.request.urlopen(url, timeout=20) as response:
        if not response.url.startswith('https://'):
            raise ValueError('Non-HTTPS redirect')
        data = response.read(limit + 1)
    if len(data) > limit:
        raise ValueError('Download exceeds size limit')
    return data

def validate(manifest, target):
    if manifest.get('platform') != target:
        raise ValueError('Wrong update platform')
    if not re.fullmatch('[0-9a-f]{40}', manifest.get('version', '')):
        raise ValueError('Invalid version')
    if not re.fullmatch('[0-9a-f]{64}', manifest.get('sha256', '')):
        raise ValueError('Invalid checksum')
    if type(manifest.get('build')) is not int or manifest['build'] <= 0:
        raise ValueError('Invalid build order')
    return BASE + target + '-' + manifest['version'] + '/' + target + '.tgz'

def unpack(archive, destination):
    total = 0
    with tarfile.open(archive, 'r:gz') as bundle:
        for member in bundle:
            name = Path(member.name)
            if name.is_absolute() or '..' in name.parts or not (member.isfile() or member.isdir()):
                raise ValueError('Unsafe archive entry')
            total += member.size
            if total > LIMIT or len(name.parts) > 5:
                raise ValueError('Unpacked update exceeds limits')
            target = destination / name
            if member.isdir():
                target.mkdir(parents=True, exist_ok=True)
            else:
                target.parent.mkdir(parents=True, exist_ok=True)
                with bundle.extractfile(member) as source, target.open('wb') as output:
                    shutil.copyfileobj(source, output)
                target.chmod(0o755 if member.mode & 0o111 else 0o644)
    for required in ('greenlink', 'greenlink-catalog', 'greenlink-player', 'linux-controls.lua', 'update.sh', 'update.py'):
        if not (destination / required).is_file():
            raise ValueError('Incomplete release')
    for binary in ('greenlink', 'greenlink-catalog', 'greenlink-player'):
        with (destination / binary).open('rb') as f:
            header = f.read(20)
        machine = int.from_bytes(header[18:20], 'little')
        if header[:6] != b'\x7fELF\x02\x01' or machine != {'x86_64': 62, 'aarch64': 183}.get(platform.machine()):
            raise ValueError('Wrong executable architecture')

def update(home, download=fetch):
    target = 'linux-' + platform.machine()
    if target not in ('linux-x86_64', 'linux-aarch64'):
        raise ValueError('Unsupported Linux architecture')
    installed = json.loads((home / 'current' / 'build.json').read_text())
    remote = json.loads(download(BASE + target + '-latest/' + target + '.json', 4096))
    url = validate(remote, target)
    if remote['build'] <= installed['build'] or remote['version'] == installed['version']:
        print('Already up to date.'); return
    releases = home / 'releases'; releases.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.download-', dir=home) as temp:
        tmp = Path(temp); data = download(url, LIMIT)
        if hashlib.sha256(data).hexdigest() != remote['sha256']:
            raise ValueError('Checksum mismatch')
        archive = tmp / 'release.tgz'; archive.write_bytes(data)
        stage = tmp / 'app'; stage.mkdir(); unpack(archive, stage)
        built = json.loads((stage / 'build.json').read_text())
        if any(built.get(k) != remote[k] for k in ('platform', 'version', 'build')):
            raise ValueError('Release metadata mismatch')
        for name in ('greenlink', 'greenlink-player'):
            check = subprocess.run([str(stage / name), '--version'], capture_output=True, text=True, timeout=5)
            if check.returncode or check.stdout.strip() != remote['version']:
                raise ValueError('New executable cannot start on this system')
        destination = releases / remote['version']
        if destination.exists():
            raise ValueError('Release directory already exists; installed version retained')
        stage.rename(destination)
        old = os.readlink(home / 'current')
        previous = home / '.previous-next'; previous.unlink(missing_ok=True); previous.symlink_to(old)
        os.replace(previous, home / 'previous')
        link = home / '.current-next'; link.unlink(missing_ok=True); link.symlink_to('releases/' + remote['version'])
        os.replace(link, home / 'current')
    print('Installed ' + remote['version'] + '; previous release retained.')

if __name__ == '__main__':
    import fcntl
    home = Path(os.environ['FLXTR_HOME']).resolve()
    with (home / '.update-lock').open('a') as lock:
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
            update(home)
        except Exception as error:
            print('Keeping installed version: ' + str(error))
            raise SystemExit(1)

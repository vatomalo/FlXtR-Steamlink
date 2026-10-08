#!/usr/bin/env python3
"""Download verified Linux releases; install user-owned app and a sudo launcher."""
import argparse
import fcntl
import importlib.util
import json
import hashlib
import os
from pathlib import Path
import platform
import shlex
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location('updater', ROOT / 'packaging/linux-update.py')
updater = importlib.util.module_from_spec(spec); spec.loader.exec_module(updater)

def install(manifest, archive, home, launcher_install=True):
    home = home.expanduser().resolve(); home.mkdir(parents=True, exist_ok=True)
    with (home / '.update-lock').open('a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        install_locked(manifest, archive, home, launcher_install)

def install_locked(manifest, archive, home, launcher_install):
    releases = home / 'releases'; releases.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.install-', dir=home) as temporary:
        stage = Path(temporary) / 'app'; stage.mkdir()
        updater.unpack(archive, stage)
        metadata = json.loads((stage / 'build.json').read_text())
        if any(metadata.get(k) != manifest[k] for k in ('version', 'platform', 'build')):
            raise ValueError('Build metadata mismatch')
        for name in ('greenlink', 'greenlink-player'):
            result = subprocess.run([str(stage / name), '--version'], capture_output=True, text=True, timeout=5)
            if result.returncode or result.stdout.strip() != manifest['version']:
                raise RuntimeError('Executable cannot start. Install dependencies with autobuild --deps, or build on this distribution with --build.')
        current = home / 'current'
        if current.exists():
            old = json.loads((current / 'build.json').read_text())
            if old['build'] > manifest['build']:
                raise ValueError('Refusing to replace a newer installed build')
            if not current.is_symlink():
                raise ValueError('Existing current directory is not a managed symlink')
        destination = releases / manifest['version']
        if not destination.exists():
            stage.rename(destination)
        elif json.loads((destination / 'build.json').read_text()) != metadata:
            raise ValueError('Conflicting release directory; nothing overwritten')
        if current.is_symlink() and current.resolve() != destination:
            backup = home / '.previous-next'; backup.unlink(missing_ok=True); backup.symlink_to(os.readlink(current))
            os.replace(backup, home / 'previous')
        next_link = home / '.current-next'; next_link.unlink(missing_ok=True)
        next_link.symlink_to('releases/' + manifest['version']); os.replace(next_link, current)
        launcher = home / 'FlXtR.sh'
        shutil.copyfile(ROOT / 'packaging/flxtr-linux.sh', launcher); launcher.chmod(0o755)
        if launcher_install:
            wrapper = Path(temporary) / 'flxtr'
            wrapper.write_text('#!/bin/sh\nexec ' + shlex.quote(str(launcher)) + ' "$@"\n')
            subprocess.run(['sudo', 'install', '-m', '755', str(wrapper), '/usr/local/bin/flxtr'], check=True)
    print('Installed: ' + str(home))
    print('Run: flxtr')

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--local', action='store_true')
    parser.add_argument('--install', action='store_true')
    args = parser.parse_args()
    target = 'linux-' + platform.machine()
    if target not in ('linux-x86_64', 'linux-aarch64'):
        raise ValueError('Unsupported Linux architecture')
    directory = Path(os.environ.get('FLXTR_DOWNLOAD_DIR', str(ROOT / 'dist/linux-downloads'))).expanduser()
    directory.mkdir(parents=True, exist_ok=True)
    if args.local:
        manifest = json.loads((ROOT / 'dist' / (target + '.json')).read_text())
        payload = (ROOT / 'dist' / (target + '.tgz')).read_bytes()
        updater.validate(manifest, target)
    else:
        manifest = json.loads(updater.fetch(updater.BASE + target + '-latest/' + target + '.json', 4096))
        url = updater.validate(manifest, target)
        payload = updater.fetch(url, updater.LIMIT)
    if hashlib.sha256(payload).hexdigest() != manifest['sha256']:
        raise ValueError('Downloaded executable package failed its checksum')
    archive = directory / (target + '-' + manifest['version'] + '.tgz')
    temp = archive.with_suffix('.part'); temp.write_bytes(payload); os.replace(temp, archive)
    archive.with_suffix('.json').write_text(json.dumps(manifest) + '\n')
    print('Verified download: ' + str(archive))
    if args.install:
        home = Path(os.environ.get('FLXTR_INSTALL_DIR', str(Path(os.environ.get('XDG_DATA_HOME', str(Path.home() / '.local/share'))) / 'flxtr')))
        install(manifest, archive, home)

if __name__ == '__main__':
    main()

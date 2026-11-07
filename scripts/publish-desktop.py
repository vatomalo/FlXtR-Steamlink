#!/usr/bin/env python3
"""Publish a verified, complete desktop archive without touching TV channels."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('platform', choices=('linux', 'windows'))
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    os.chdir(root)
    commit = subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True).strip()
    if subprocess.check_output(['git', 'status', '--porcelain']).strip():
        raise SystemExit('Source changed; refusing to publish desktop package.')
    receipt = json.loads((root / 'dist' / (args.platform + '-desktop-build.json')).read_text())
    expected = {'linux': 'FlXtR-Linux-x86_64.tgz', 'windows': 'FlXtR-Windows-x86_64.zip'}[args.platform]
    file = root / 'dist' / expected
    if receipt['commit'] != commit or receipt['artifact'] != expected or receipt['architecture'] != 'x86_64':
        raise SystemExit('Desktop package does not match this source/architecture. Rebuild first.')
    with file.open('rb') as stream:
        digest = hashlib.file_digest(stream, 'sha256').hexdigest()
    if receipt['sha256'] != digest or receipt['size'] != file.stat().st_size:
        raise SystemExit('Desktop package checksum differs from its build receipt.')
    repo = 'vatomalo/FlXtR-Steamlink'
    tag = f'desktop-{args.platform}-x86_64-{commit}'
    title = os.environ.get('FLXTR_RELEASE_TITLE') or f'FlXtR {args.platform.title()} x86-64 — {commit[:8]}'
    notes_path = os.environ.get('FLXTR_RELEASE_NOTES_FILE')
    notes = Path(notes_path).read_text() if notes_path else 'Complete desktop application package. Extract the archive and run ' + ('FlXtR.sh.' if args.platform == 'linux' else 'FlXtR.exe.')
    notes += f'\n\nSource: `{commit}`\nPlatform: {args.platform} x86-64\n'
    exists = subprocess.run(['gh', 'release', 'view', tag, '-R', repo, '--json', 'isDraft'], capture_output=True, text=True)
    with tempfile.TemporaryDirectory(prefix='flxtr-desktop-release-') as temporary:
        notes_file = Path(temporary) / 'notes.md'
        notes_file.write_text(notes)
        if exists.returncode == 0 and not json.loads(exists.stdout)['isDraft']:
            # The payload stays immutable; explicitly supplied release text can change.
            if notes_path or os.environ.get('FLXTR_RELEASE_TITLE'):
                subprocess.run(['gh', 'release', 'edit', tag, '-R', repo, '--title', title, '--notes-file', str(notes_file)], check=True)
            print('Package already published; existing download kept.')
        else:
            if exists.returncode:
                subprocess.run(['gh', 'release', 'create', tag, '-R', repo, '--target', commit, '--draft', '--prerelease',
                                '--title', title, '--notes-file', str(notes_file)], check=True)
            checksum = Path(temporary) / (expected + '.sha256')
            checksum.write_text(f'{digest}  {expected}\n')
            subprocess.run(['gh', 'release', 'upload', tag, '-R', repo, str(file), str(checksum), '--clobber'], check=True)
            subprocess.run(['gh', 'release', 'edit', tag, '-R', repo, '--draft=false', '--prerelease',
                            '--title', title, '--notes-file', str(notes_file)], check=True)
    print(f'https://github.com/{repo}/releases/tag/{tag}')


if __name__ == '__main__':
    main()

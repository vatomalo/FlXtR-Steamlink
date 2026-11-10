#!/usr/bin/env python3
"""Bundle pinned pure-Python YouTube dependencies and a native QuickJS engine."""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import urllib.request
from zipfile import ZipFile
ROOT = Path(__file__).resolve().parent.parent
sources = [
    ('yt-dlp-LICENSE', 'https://raw.githubusercontent.com/yt-dlp/yt-dlp/2026.08.19/LICENSE',
     '7e12e5df4bae12cb21581ba157ced20e1986a0508dd10d0e8a4ab9a4cf94e85c'),
    ('yt-dlp', 'https://github.com/yt-dlp/yt-dlp/releases/download/2026.08.19/yt-dlp',
     '1fa6733c37ea6fb51c99ad8fe785e7b7e5f3246c9b980230329d4fb72ed8d4d6'),
    ('ejs.whl', 'https://files.pythonhosted.org/packages/e3/bd/520769863744b669440a924271a6159ddd82ad5ae26b4ac4d4b69e9f8d44/yt_dlp_ejs-0.8.0-py3-none-any.whl',
     '79300e5fca7f937a1eeede11f0456862c1b41107ce1d726871e0207424f4bdb4'),
    ('quickjs-2026-06-04.tar.xz', 'https://bellard.org/quickjs/quickjs-2026-06-04.tar.xz',
     'b376e839b322978313d929fd20663b11ba58b75df5a46c126dd19ea2fa70ad2a'),
]
def main(app):
    cache = ROOT / 'build/desktop-music'
    cache.mkdir(parents=True, exist_ok=True)
    for name, url, digest in sources:
        dest = cache / name
        if not dest.exists():
            with urllib.request.urlopen(url, timeout=60) as response:
                dest.write_bytes(response.read())
        if hashlib.sha256(dest.read_bytes()).hexdigest() != digest:
            raise SystemExit('Dependency checksum mismatch: ' + name)
    runtime = app / 'music-runtime'
    modules = runtime / 'site-packages'
    modules.mkdir(parents=True, exist_ok=True)
    subprocess.run([sys.executable, str(ROOT / 'tools/package_music.py'),
                    str(cache / 'yt-dlp'), str(modules)], check=True)
    with ZipFile(cache / 'ejs.whl') as archive:
        archive.extractall(modules)
    source = cache / 'quickjs-2026-06-04'
    if not source.exists():
        with tarfile.open(cache / 'quickjs-2026-06-04.tar.xz') as archive:
            archive.extractall(cache)
    subprocess.run(['make', '-j2', 'qjs'], cwd=source, check=True)
    (runtime / 'bin').mkdir(exist_ok=True)
    shutil.copy2(source / 'qjs', runtime / 'bin/qjs')
    shutil.copy2(source / 'LICENSE', runtime / 'QUICKJS-LICENSE')
    (runtime / 'SOURCES.json').write_text(json.dumps(sources, indent=2) + '\n')
    shutil.copy2(cache / 'yt-dlp-LICENSE', runtime / 'YT-DLP-LICENSE')
    # Preserve the dependency's own licenses, including the EJS wheel metadata.
    with ZipFile(cache / 'yt-dlp') as archive:
        for name in archive.namelist():
            if name.upper().endswith(('LICENSE', 'LICENSE.TXT', 'LICENSE.MD', 'COPYING')):
                archive.extract(name, runtime / 'licenses')
    subprocess.run([sys.executable, '-c', 'import yt_dlp, yt_dlp_ejs; print(yt_dlp.version.__version__)'],
                   env=dict(__import__('os').environ, PYTHONPATH=str(modules)), check=True)
if __name__ == '__main__':
    main(Path(sys.argv[1]))

#!/usr/bin/env python3
"""Fetch the pinned UCRT Windows headers/runtime into build/windows-sdk (WSL)."""
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
from pathlib import Path, PurePosixPath
import shutil
import subprocess
import tarfile
import urllib.request
ROOT = Path(__file__).resolve().parents[1]
SDK = ROOT / 'build/windows-sdk'
LOCK = ROOT / 'scripts/windows-sdk.lock.json'
def download(url, path, digest):
    if path.exists() and hashlib.sha256(path.read_bytes()).hexdigest() == digest:
        return
    temp = path.with_suffix(path.suffix + '.part')
    with urllib.request.urlopen(url, timeout=90) as response, temp.open('wb') as out:
        shutil.copyfileobj(response, out)
    if hashlib.sha256(temp.read_bytes()).hexdigest() != digest:
        raise ValueError('Checksum mismatch: ' + url)
    temp.replace(path)
def package(item):
    cache = SDK / 'cache' / item['filename']
    download(item['url'], cache, item['sha256'])
    marker = SDK / 'installed' / (item['name'] + '.sha256')
    if marker.exists() and marker.read_text() == item['sha256']:
        return item['name']
    process = subprocess.Popen(['zstd', '-dc', str(cache)], stdout=subprocess.PIPE)
    with tarfile.open(fileobj=process.stdout, mode='r|') as bundle:
        for entry in bundle:
            relative = PurePosixPath(entry.name)
            if not relative.parts or relative.parts[0] != 'ucrt64':
                continue
            if relative.is_absolute() or '..' in relative.parts:
                raise ValueError('Unsafe archive path')
            target = SDK.joinpath(*relative.parts)
            if entry.isdir():
                target.mkdir(parents=True, exist_ok=True)
            elif entry.isfile():
                target.parent.mkdir(parents=True, exist_ok=True)
                with bundle.extractfile(entry) as src, target.open('wb') as out:
                    shutil.copyfileobj(src, out)
            elif entry.issym():
                link = PurePosixPath(entry.linkname)
                resolved = (target.parent / entry.linkname).resolve()
                if link.is_absolute() or not resolved.is_relative_to((SDK / 'ucrt64').resolve()):
                    raise ValueError('Unsafe SDK symlink')
                target.parent.mkdir(parents=True, exist_ok=True)
                target.unlink(missing_ok=True)
                target.symlink_to(entry.linkname)
    if process.wait():
        raise RuntimeError('zstd extraction failed')
    marker.parent.mkdir(parents=True, exist_ok=True)
    marker.write_text(item['sha256'])
    return item['name']
def main():
    SDK.mkdir(parents=True, exist_ok=True)
    (SDK / 'cache').mkdir(exist_ok=True)
    lock = json.loads(LOCK.read_text())
    compiler = lock['compiler']
    archive = SDK / 'cache/llvm-mingw.tar.xz'
    download(compiler['url'], archive, compiler['sha256'])
    compiler_root = SDK / compiler['directory']
    if not compiler_root.exists():
        # This digest-pinned compiler archive contains its own relative symlinks.
        with tarfile.open(archive) as bundle:
            bundle.extractall(SDK, filter='data')
    with ThreadPoolExecutor(max_workers=4) as pool:
        for n, name in enumerate(pool.map(package, lock['packages']), 1):
            if n % 20 == 0 or n == len(lock['packages']):
                print(f"Windows SDK: {n}/{len(lock['packages'])} packages ready", flush=True)
    print('Windows compiler: ' + str(compiler_root / 'bin/x86_64-w64-mingw32-clang'))
if __name__ == '__main__':
    main()

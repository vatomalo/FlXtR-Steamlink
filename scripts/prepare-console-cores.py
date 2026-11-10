#!/usr/bin/env python3
"""Fetch the pinned official Libretro cores used by the Android frontend.

Archives are cached under android/build and verified before extraction. Updating
the lock file is an explicit maintainer operation, never an app-side download.
"""
import hashlib, io, json, os, urllib.request, zipfile
from pathlib import Path
root=Path(__file__).resolve().parents[1]
lock=json.loads((root/'android/console-cores.lock.json').read_text())
abis=os.environ.get('FLXTR_CORE_ABIS','arm64-v8a,armeabi-v7a').split(',')
cache=root/'android/build/console-core-cache';cache.mkdir(parents=True,exist_ok=True)
for abi in abis:
    for core in lock['cores']:
        if core=='fceumm':
            stale=root/'android/app/src/main/jniLibs'/abi/'libflxtr_fceumm.so'
            stale.unlink(missing_ok=True)
            continue # Built from the same pinned sources as the TV handoff core.
        item=lock['cores'][core][abi]
        archive=cache/f'{core}-{abi}.zip'
        if not archive.is_file():
            with urllib.request.urlopen(item['url'],timeout=60) as response: data=response.read()
            if hashlib.sha256(data).hexdigest()!=item['sha256']:raise RuntimeError(f'{core}/{abi}: upstream changed; update the lock deliberately')
            archive.write_bytes(data)
        data=archive.read_bytes()
        if hashlib.sha256(data).hexdigest()!=item['sha256']:raise RuntimeError(f'{archive}: checksum mismatch')
        with zipfile.ZipFile(io.BytesIO(data)) as z:
            binary=z.read(f'{core}_libretro_android.so')
        if binary[:4]!=b'\x7fELF':raise RuntimeError('Invalid core binary')
        target=root/'android/app/src/main/jniLibs'/abi/f'libflxtr_{core}.so'
        target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(binary)
print('Verified console cores ready')

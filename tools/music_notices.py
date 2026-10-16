"""Collect redistribution notices for the isolated audio lookup runtime."""
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import sys
import io
import tarfile
import urllib.request

root = Path(sys.argv[1]) / 'LICENSES'
root.mkdir(parents=True, exist_ok=True)
licenses = {
    'Python.txt': 'https://raw.githubusercontent.com/python/cpython/v3.12.15/LICENSE',
    'yt-dlp.txt': 'https://raw.githubusercontent.com/yt-dlp/yt-dlp/2026.08.19/LICENSE',
    'yt-dlp-ejs.txt': 'https://raw.githubusercontent.com/yt-dlp/ejs/master/LICENSE',
    'QuickJS.txt': 'https://raw.githubusercontent.com/bellard/quickjs/master/LICENSE',
    'musl.txt': 'https://git.musl-libc.org/cgit/musl/plain/COPYRIGHT?h=v1.2.5',
    'OpenSSL.txt': 'https://raw.githubusercontent.com/openssl/openssl/openssl-3.5.9/LICENSE.txt',
    'libffi.txt': 'https://raw.githubusercontent.com/libffi/libffi/v3.5.2/LICENSE',
    'expat.txt': 'https://raw.githubusercontent.com/libexpat/libexpat/R_2_8_5/expat/COPYING',
    'bzip2.txt': 'https://sourceware.org/pub/bzip2/bzip2-1.0.8.tar.gz',
    'xz.txt': 'https://raw.githubusercontent.com/tukaani-project/xz/v5.8.4/COPYING',
    'zlib.txt': 'https://raw.githubusercontent.com/madler/zlib/v1.3.2/LICENSE',
    'ncurses.txt': 'https://raw.githubusercontent.com/mirror/ncurses/master/COPYING',
    'mpdecimal.txt': 'https://www.bytereef.org/software/mpdecimal/releases/mpdecimal-4.0.1.tar.gz',
    'GCC-GPL3.txt': 'https://raw.githubusercontent.com/gcc-mirror/gcc/releases/gcc-15.2.0/COPYING3',
    'GCC-runtime-exception.txt': 'https://raw.githubusercontent.com/gcc-mirror/gcc/releases/gcc-15.2.0/COPYING.RUNTIME',
}

def fetch(item):
    name, url = item
    try:
        with urllib.request.urlopen(url, timeout=30) as response:
            data = response.read(5000000)
        if url.endswith('.tar.gz'):
            with tarfile.open(fileobj=io.BytesIO(data), mode='r:gz') as source:
                candidates = [member for member in source.getmembers() if member.isfile() and len(Path(member.name).parts) == 2 and Path(member.name).name in ('LICENSE', 'LICENSE.txt', 'COPYRIGHT.txt', 'COPYING')]
                if not candidates:
                    raise ValueError('No top-level license found in source archive')
                data = b'\n\n'.join(source.extractfile(member).read() for member in candidates)
    except Exception as error:
        return f'{name}: {error}'
    (root / name).write_bytes(data)

with ThreadPoolExecutor(max_workers=5) as pool:
    errors = [error for error in pool.map(fetch, licenses.items()) if error]
if errors:
    raise SystemExit('\n'.join(errors))
(root / 'SOURCES.txt').write_text('\n'.join(f'{name}: {url}' for name, url in licenses.items()) + '\nSQLite is public domain: https://www.sqlite.org/copyright.html\nPackage versions and Alpine provenance are alongside this directory.\nGDBM and Readline libraries/modules are excluded from the runtime.\n')

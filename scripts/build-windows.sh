#!/usr/bin/env bash
# Cross-compile the Windows platform/IPC/phone-server validation executables.
# For the complete app run: python3 scripts/build-windows-app.py
set -euo pipefail
cd "$(dirname "$0")/.."
compiler=${WINDOWS_CC:-x86_64-w64-mingw32-gcc}
if ! command -v "$compiler" >/dev/null; then
    echo "Missing $compiler. Install gcc-mingw-w64-x86-64-posix in Debian/Ubuntu WSL." >&2
    exit 1
fi
header=${MPV_HEADER:-/usr/include/mpv/client.h}
[[ -f "$header" ]] || { echo "Missing $header. Install libmpv-dev for the player cross-compile check." >&2; exit 1; }
mkdir -p build/windows-platform build/windows-headers/mpv dist
flags=(-std=c99 -O2 -Wall -Wextra -Werror -D_WIN32_WINNT=0x0601 -static)
"$compiler" "${flags[@]}" -municode tests/platform_test.c -lbcrypt \
    -o build/windows-platform/FlXtR-windows-platform-test.exe
"$compiler" "${flags[@]}" tests/platform_ipc_test.c -lbcrypt -ladvapi32 \
    -o build/windows-platform/FlXtR-windows-ipc-test.exe
"$compiler" "${flags[@]}" tests/api_setup_server.c -lbcrypt -ladvapi32 -lws2_32 -liphlpapi \
    -o build/windows-platform/FlXtR-phone-server-test.exe
# libmpv's public API header is platform independent. Validate the real player
# source with the Windows compiler; Windows import libraries/DLLs come later.
cp "$header" build/windows-headers/mpv/client.h
"$compiler" "${flags[@]}" -Ibuild/windows-headers -c src/player_linux.c \
    -o build/windows-platform/player-desktop.o
python3 - <<'PY'
from pathlib import Path
import hashlib
import zipfile
root = Path('build/windows-platform')
readme = (
    'FlXtR Windows port: steps 1 and 2, platform + IPC + phone server\n\n'
    'Developer validation executables; the full app is a separate Windows ZIP.\n'
    'In PowerShell, run from a writable extracted folder:\n'
    '  .\\FlXtR-windows-platform-test.exe\n'
    '  .\\FlXtR-windows-ipc-test.exe\n'
    'Expected: PASS messages and exit status 0. Neither executable plays media.\n\n'
    'Optional actual phone HTTP server:\n'
    '  .\\FlXtR-phone-server-test.exe\n'
    'It prints a temporary localhost URL for the setup/phone pages. Stop with Ctrl+C.\n'
    'This fixture binds only 127.0.0.1; it is not a LAN phone-remote release.\n'
    'Any files/keys deliberately saved through those pages remain in this folder.\n\n'
    'Implemented: native processes, private nonblocking pipes, preload/control IPC,\n'
    'Winsock HTTP setup/uploads, UTF-8 paths, private temp files and data paths.\n'
    'This package checks the player source with the Windows compiler. Full\n'
    'application builds are available separately via build-windows-app.py.\n'
    'Build host: Debian/Ubuntu WSL with MinGW-w64. No Docker or C# required.\n'
    'Source/status: https://github.com/vatomalo/FlXtR-Steamlink/blob/port/windows-platform/docs/WINDOWS.md\n'
)
(root / 'README.txt').write_text(readme)
archive = Path('dist/FlXtR-Windows-platform-x86_64.zip')
with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED) as bundle:
    for name in ('FlXtR-windows-platform-test.exe', 'FlXtR-windows-ipc-test.exe',
                 'FlXtR-phone-server-test.exe', 'README.txt'):
        bundle.write(root / name, name)
    bundle.write('assets/phone.html', 'assets/phone.html')
    bundle.write('assets/phone.js', 'assets/phone.js')
    bundle.write('LICENSE', 'LICENSE')
digest = hashlib.sha256(archive.read_bytes()).hexdigest()
Path(str(archive) + '.sha256').write_text(digest + '  ' + archive.name + '\n')
print('Built ' + str(archive))
print('Developer foundation tests; complete application: scripts/build-windows-app.py')
PY

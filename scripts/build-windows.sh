#!/usr/bin/env bash
# Step 1 of the Windows port: cross-compile the platform validation executable.
# The SDL UI/catalog/player are not yet Windows build targets.
set -euo pipefail
cd "$(dirname "$0")/.."
compiler=${WINDOWS_CC:-x86_64-w64-mingw32-gcc}
if ! command -v "$compiler" >/dev/null; then
    echo "Missing $compiler. In Debian/Ubuntu WSL install gcc-mingw-w64-x86-64-posix." >&2
    exit 1
fi
mkdir -p build/windows-platform dist
"$compiler" -std=c99 -O2 -Wall -Wextra -Werror -D_WIN32_WINNT=0x0601 \
    -municode -static tests/platform_test.c -lbcrypt \
    -o build/windows-platform/FlXtR-windows-platform-test.exe
python3 - <<'PY'
from pathlib import Path
import hashlib
import zipfile
root = Path('build/windows-platform')
readme = (
    'FlXtR Windows port: step 1, platform foundation\n\n'
    'This is a developer validation executable, NOT the complete FlXtR app.\n'
    'Run FlXtR-windows-platform-test.exe from PowerShell in a writable folder.\n'
    'It starts test children and checks UTF-8/quoted arguments, stream redirection,\n'
    'handle isolation, timeouts, termination, randomness, file locks and replacement.\n'
    'Expected: PASS messages and exit status 0. The program does not play media.\n'
    'The UI/catalog/player, Windows IPC/network code and installer remain pending.\n'
    'Build host: Debian/Ubuntu WSL with MinGW-w64. No Docker or C# required.\n'
    'Source and port status: https://github.com/vatomalo/FlXtR-Steamlink\n'
)
(root / 'README.txt').write_text(readme)
archive = Path('dist/FlXtR-Windows-platform-x86_64.zip')
with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED) as bundle:
    bundle.write(root / 'FlXtR-windows-platform-test.exe', 'FlXtR-windows-platform-test.exe')
    bundle.write(root / 'README.txt', 'README.txt')
    bundle.write('LICENSE', 'LICENSE')
digest = hashlib.sha256(archive.read_bytes()).hexdigest()
Path(str(archive) + '.sha256').write_text(digest + '  ' + archive.name + '\n')
print('Built ' + str(archive))
print('Platform foundation only: the complete Windows application is not ready.')
PY

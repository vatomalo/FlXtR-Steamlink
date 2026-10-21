#!/usr/bin/env python3
"""Bundle desktop dependencies, retaining system glibc and graphics drivers."""
import hashlib
import json
import re
import shutil
import subprocess
import tarfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DIST = ROOT / 'dist'
version = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
source = DIST / 'FlXtR-linux-x86_64'
names = ('greenlink', 'greenlink-catalog', 'greenlink-player', 'greenlink-resolver', 'greenlink-audio')
# These components must match the host kernel, glibc and installed GPU drivers.
system = re.compile(r'^(ld-linux|lib(c|m|dl|pthread|rt|resolv|nss_[^.]+)\.so|lib(stdc\+\+|gcc_s)\.so|lib(GL|EGL|GLES|GLX|OpenGL|GLdispatch|glapi|drm|gbm|vulkan|va)[-.0-9a-z_]*\.so)')
for edition in ('Linux', 'SteamDeck'):
    name = 'FlXtR-' + edition + '-x86_64'
    dest = DIST / name
    if dest.exists():
        raise SystemExit(str(dest) + ' already exists; move it aside before rebuilding')
    dest.mkdir()
    app = dest / 'releases' / version
    shutil.copytree(source / 'releases' / version, app)
    shutil.copy2(source / 'FlXtR.sh', dest / 'FlXtR.sh')
    (dest / 'current').symlink_to('releases/' + version)
    lib = app / 'lib'
    lib.mkdir()
    licenses = app / 'dependency-licenses'
    licenses.mkdir()
    packages = set()
    for binary in names:
        report = subprocess.check_output(['ldd', str(app / binary)], text=True)
        if 'not found' in report:
            raise SystemExit('Unresolved dependencies: ' + report)
        for line in report.splitlines():
            match = re.match(r'\s*(\S+) => (/\S+)', line)
            if not match:
                continue
            soname, path = match.groups()
            if system.match(soname):
                continue
            shutil.copy2(path, lib / soname)
            owner = subprocess.run(['dpkg-query', '-S', path], capture_output=True, text=True)
            if owner.returncode:
                owner = subprocess.run(['dpkg-query', '-S', str(Path(path).resolve())], capture_output=True, text=True)
            if owner.returncode:
                raise SystemExit('Cannot find package license for ' + path)
            package = owner.stdout.split(': ')[0].split(':')[0]
            packages.add(package)
        executable = app / binary
        executable.rename(app / (binary + '.bin'))
        executable.write_text('#!/bin/sh\napp=$(dirname -- "$(readlink -f -- "$0")")\n'
                              'if [ -z "${FLXTR_HOST_LIBRARY_PATH_SET+x}" ]; then\n'
                              '  export FLXTR_HOST_LIBRARY_PATH="${LD_LIBRARY_PATH-}"\n'
                              '  export FLXTR_HOST_LIBRARY_PATH_SET=1\n'
                              'fi\n'
                              'export LD_LIBRARY_PATH="$app/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"\n'
                              'exec "$app/' + binary + '.bin" "$@"\n')
        executable.chmod(0o755)
    for package in sorted(packages):
        copyright = Path('/usr/share/doc') / package / 'copyright'
        if not copyright.is_file():
            raise SystemExit('Missing license: ' + package)
        shutil.copy2(copyright, licenses / (package + '.copyright'))
    (licenses / 'packages.txt').write_text(subprocess.check_output(
        ['dpkg-query', '-W', '-f=${Package} ${Version}\n', *sorted(packages)], text=True))
    data = dest / 'data'
    data.mkdir()
    (data / '.no-auto-update').touch()
    if edition == 'SteamDeck':
        launcher = dest / 'FlXtR-SteamDeck.sh'
        launcher.write_text('#!/bin/sh\nexport SDL_VIDEODRIVER=x11\n'
                            'export FLXTR_STEAMDECK=1\nunset WAYLAND_DISPLAY\n'
                            'root=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)\n'
                            'exec "$root/FlXtR.sh" "$@"\n')
        launcher.chmod(0o755)
    (dest / 'README.txt').write_text(
        'FlXtR ' + edition + ' beta, x86_64\n\n'
        'Extract into a writable folder. Run ' +
        ('FlXtR-SteamDeck.sh' if edition == 'SteamDeck' else 'FlXtR.sh') + '.\n'
        'Steam Deck: in Desktop Mode, add FlXtR-SteamDeck.sh to Steam as a non-Steam game.\n'
        'Leave compatibility/Proton disabled. Use the Gamepad controller layout.\n'
        'The Deck launcher uses X11/EGL and software video decoding for compatibility.\n'
        'For troubleshooting, share the last lines of data/greenlink.log.\n'
        'Built on Ubuntu 22.04; requires glibc 2.35+, Python 3.10+, system certificates,\n'
        'a desktop display/audio session and system OpenGL/VA-API/Vulkan drivers.\n'
        'Music includes yt-dlp, EJS and QuickJS.\n'
        'Application libraries are bundled under current/lib with dependency licenses.\n'
        'Automatic updates are disabled to retain bundled libraries. For upgrades,\n'
        'extract a new release separately and copy your data folder across.\n'
        'Games > DOWNLOAD LIBRETRO CORES installs missing cores. RetroArch is required.\n'
        'See current/CORES.md for native/Flatpak setup and data paths.\n'
        'Physical Steam Deck and interactive playback validation remains necessary.\n')
    for binary in ('greenlink', 'greenlink-player'):
        output = subprocess.check_output([str(app / binary), '--version'], text=True).strip()
        if output != version:
            raise SystemExit('Version smoke test failed: ' + binary)
    archive = DIST / (name + '.tgz')
    with tarfile.open(archive, 'w:gz') as tar:
        tar.add(dest, arcname=name)
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    (DIST / (name + '.tgz.sha256')).write_text(digest + '  ' + archive.name + '\n')
    print('Packaged ' + archive.name, flush=True)

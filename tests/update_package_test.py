"""A stale version marker must not hide overwritten launchers or runtimes."""
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import tarfile
import tempfile

root = Path(__file__).resolve().parent.parent
with tempfile.TemporaryDirectory() as temp:
    app = Path(temp)
    stage = app / 'fixture' / 'greenlink'
    stage.mkdir(parents=True)
    (app / 'bin').mkdir()
    (app / 'bin/curl').write_text('''#!/bin/sh
for arg in "$@"; do
  case "$arg" in *package-manifest.txt) src=manifest;; */greenlink.tgz) src=package.tgz;; esac
  out="$arg"
done
cp "$src" "$out"
''')
    (app / 'bin/curl').chmod(0o755)
    version = '2' * 40
    (app / 'fixture.c').write_text(f'#include <stdio.h>\nint main(void){{puts("{version}");return 0;}}\n')
    subprocess.run(['cc', str(app / 'fixture.c'), '-o', str(stage / 'greenlink')], check=True)
    shutil.copy(stage / 'greenlink', stage / 'greenlink-catalog')
    shutil.copy(root / 'packaging/update.sh', stage / 'update.sh')
    (stage / 'greenlink.sh').write_text('#!/bin/sh\necho fixed\n')
    (stage / 'retroarch-libs').mkdir()
    (stage / 'retroarch-libs/libSDL2-2.0.so.0').write_bytes(b'native SDL fixture')
    (stage / 'assets').mkdir()
    (stage / 'assets/music-required-v1').write_text('1\n')
    shutil.copy(stage / 'greenlink', stage / 'greenlink-audio')
    for name in ('scripts/music-runtime.sh', 'scripts/music-qjs.sh', 'tools/music.py', 'music-runtime/usr/bin/python3'):
        path = stage / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text('#!/bin/sh\nexit 0\n')
        path.chmod(0o755)
    module = stage / 'music-runtime/example.py'
    module.write_text('answer = 42\n')
    os.utime(module, (1600000000, 1600000000))
    with tarfile.open(app / 'package.tgz', 'w:gz') as archive:
        archive.add(stage, arcname='greenlink')
    digest = hashlib.sha256((app / 'package.tgz').read_bytes()).hexdigest()
    (app / 'manifest').write_text(f'version={version}\nsha256={digest}\n')
    shutil.copy(stage / 'greenlink', app / 'greenlink')
    shutil.copy(stage / 'update.sh', app / 'update.sh')
    (app / '.installed-package-version').write_text(version + '\n')
    (app / 'settings.cfg').write_text('private settings\n')
    env = dict(os.environ, PATH=str(app / 'bin') + ':' + os.environ['PATH'])

    def run():
        result = subprocess.run(['sh', './update.sh'], cwd=app, env=env, capture_output=True, text=True)
        assert result.returncode == 0, result.stdout + result.stderr
        assert not (app / '.update-lock').exists()
        assert (app / 'settings.cfg').read_text() == 'private settings\n'
        assert (app / 'music-runtime/example.py').stat().st_mtime == 1600000000
        return result.stdout

    assert 'Installed complete' in run()  # legacy marker without hashes
    assert 'already up to date' in run()
    # An old updater can leave the new UI installed but omit the new runtime.
    (app / 'greenlink-audio').unlink()
    hashes = app / '.installed-package-sha256'
    hashes.write_text(''.join(line for line in hashes.read_text().splitlines(True) if 'greenlink-audio' not in line))
    assert 'Installed complete' in run()
    assert (app / 'greenlink-audio').is_file()
    (app / 'music-runtime/usr/bin/python3').write_bytes(b'broken runtime')
    assert 'Installed complete' in run()
    for name in ('greenlink', 'greenlink-catalog', 'greenlink.sh', 'retroarch-libs/libSDL2-2.0.so.0'):
        (app / name).write_bytes(b'overwritten old file')
        assert 'Installed complete' in run(), name
        assert (app / name).read_bytes() == (stage / name).read_bytes(), name
    (app / 'retroarch-libs/libSDL2-2.0.so.0').unlink()
    assert 'Installed complete' in run()
    assert 'already up to date' in run()
print('PASS: package integrity, stale marker repair, missing SDL repair, settings preservation')

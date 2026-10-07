"""Exercise the real POSIX updater with deterministic downloads and ELF fixtures."""
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parent.parent
with tempfile.TemporaryDirectory() as temp:
    app = Path(temp)
    shutil.copy(root / 'packaging/update.sh', app / 'update.sh')
    (app / 'bin').mkdir()
    (app / 'bin/curl').write_text('''#!/bin/sh
[ "$FAIL_FETCH" != 1 ] || exit 22
for arg in "$@"; do
  case "$arg" in *shell-manifest.txt) src=manifest;; */greenlink) src=candidate;; esac
  out="$arg"
done
cp "$src" "$out"
''')
    (app / 'bin/curl').chmod(0o755)
    env = dict(os.environ, PATH=str(app / 'bin') + ':' + os.environ['PATH'])
    def binary(name, version):
        source = app / 'fixture.c'
        source.write_text('#include <stdio.h>\nint main(void){puts("' + version + '");return 0;}\n')
        subprocess.run(['cc', str(source), '-o', str(app / name)], check=True)
    old, new = '1' * 40, '2' * 40
    binary('greenlink', old)
    original = (app / 'greenlink').read_bytes()
    binary('candidate', new)
    candidate = (app / 'candidate').read_bytes()
    private = b'private catalog sentinel\n'
    (app / 'catalog.local.tsv').write_bytes(private)
    def manifest(version=new, digest=None):
        digest = digest or hashlib.sha256(candidate).hexdigest()
        (app / 'manifest').write_text(f'version={version}\nsha256={digest}\n')
    def run(success, **extra):
        result = subprocess.run(['sh', './update.sh'], cwd=app, env=dict(env, **extra), capture_output=True)
        assert (result.returncode == 0) == success, result.stderr
        assert not (app / '.update-lock').exists()
        assert (app / 'catalog.local.tsv').read_bytes() == private
    manifest()
    run(False, FAIL_FETCH='1')
    assert (app / 'greenlink').read_bytes() == original
    manifest(digest='0' * 64)
    run(False)
    assert (app / 'greenlink').read_bytes() == original
    manifest(version='3' * 40)
    run(False)  # valid hash, wrong executable version
    assert (app / 'greenlink').read_bytes() == original
    manifest(version='../../escape')
    run(False)
    manifest()
    run(True)
    assert (app / 'greenlink').read_bytes() == candidate
    assert (app / 'greenlink.previous').read_bytes() == original
    run(True)  # current release does not overwrite the rollback copy
    assert (app / 'greenlink.previous').read_bytes() == original
print('PASS: offline, checksum/version rejection, atomic update, backup and private catalog preservation')

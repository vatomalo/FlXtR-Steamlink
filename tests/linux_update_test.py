import hashlib
import importlib.util
import io
import json
from pathlib import Path
import platform
import tarfile
import tempfile
from unittest.mock import patch
from types import SimpleNamespace

spec = importlib.util.spec_from_file_location('updater', Path(__file__).resolve().parents[1] / 'packaging/linux-update.py')
updater = importlib.util.module_from_spec(spec); spec.loader.exec_module(updater)
target = 'linux-' + platform.machine()
version = 'b' * 40

def archive(entries):
    output = io.BytesIO()
    with tarfile.open(fileobj=output, mode='w:gz') as tar:
        for name, data in entries.items():
            entry = tarfile.TarInfo(name); entry.size = len(data); entry.mode = 0o755
            tar.addfile(entry, io.BytesIO(data))
    return output.getvalue()

header = bytearray(20); header[:6] = b'\x7fELF\x02\x01'
header[18:20] = {'x86_64': 62, 'aarch64': 183}[platform.machine()].to_bytes(2, 'little')
built = dict(platform=target, version=version, build=200)
files = {name: bytes(header) for name in ('greenlink', 'greenlink-catalog', 'greenlink-player')}
files.update({name: b'' for name in ('linux-controls.lua', 'update.sh', 'update.py')})
files['build.json'] = json.dumps(built).encode()
bundle = archive(files)
remote = dict(built, sha256=hashlib.sha256(bundle).hexdigest())

with tempfile.TemporaryDirectory() as temporary:
    home = Path(temporary); old = home / 'releases' / ('a' * 40); old.mkdir(parents=True)
    (old / 'build.json').write_text(json.dumps(dict(platform=target, version='a'*40, build=100)))
    (home / 'current').symlink_to('releases/' + 'a'*40)
    (home / 'data').mkdir(); (home / 'data/settings.cfg').write_text('keep me')
    def download(url, limit):
        return json.dumps(remote).encode() if url.endswith('.json') else bundle
    for changes in ({'platform': 'steamlink'}, {'sha256':'0'*64}):
        saved = remote.copy(); remote.update(changes)
        try: updater.update(home, download); raise AssertionError('Bad update accepted')
        except ValueError: pass
        remote = saved
        assert (home / 'current').resolve() == old
    saved = remote.copy(); remote['build'] = 99
    updater.update(home, download); assert (home / 'current').resolve() == old
    remote = saved
    with patch.object(updater.subprocess, 'run', return_value=SimpleNamespace(returncode=127, stdout='')):
        try: updater.update(home, download); raise AssertionError('Broken binary accepted')
        except ValueError: pass
    with patch.object(updater.subprocess, 'run', return_value=SimpleNamespace(returncode=0, stdout=version+'\n')):
        updater.update(home, download)
    assert (home / 'current').resolve().name == version
    assert (home / 'previous').resolve() == old
    assert (home / 'data/settings.cfg').read_text() == 'keep me'
    updater.update(home, download)
    bad = home / 'bad.tgz'; bad.write_bytes(archive({'../../escape': b'bad'}))
    try: updater.unpack(bad, home / 'stage'); raise AssertionError('Traversal accepted')
    except ValueError: pass
print('PASS: platform, rollback, checksum, startup, path validation, atomic update and data preservation')

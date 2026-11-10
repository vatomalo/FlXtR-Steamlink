#!/usr/bin/env python3
"""Versioned, byte-verified savestate shadows. Never auto-restore a state."""
from contextlib import contextmanager
import csv
import errno
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import stat
import subprocess
import sys
import tempfile
import time

ROOT = Path('.')
STATE = re.compile(r'.*\.state(?:\d+|\.auto)?$', re.I)
LIMIT = 512 * 1024 * 1024


def atomic_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(dir=path.parent, delete=False) as out:
        temp = Path(out.name)
        out.write(json.dumps(value).encode())
        out.flush()
        os.fsync(out.fileno())
    os.replace(temp, path)
    sync_dir(path.parent)


def sync_dir(path):
    if os.name != 'nt':
        fd = os.open(str(path), os.O_RDONLY)
        try:
            try: os.fsync(fd)
            except OSError as error:
                if error.errno not in (errno.EINVAL, errno.ENOTSUP, errno.EBADF): raise
        finally: os.close(fd)


@contextmanager
def locked():
    folder = ROOT / 'state-shadows'
    folder.mkdir(parents=True, exist_ok=True)
    with (folder / '.lock').open('a+b') as handle:
        if os.name == 'nt':
            import msvcrt
            handle.write(b'0');handle.flush();handle.seek(0)
            msvcrt.locking(handle.fileno(), msvcrt.LK_LOCK, 1)
        else:
            import fcntl
            fcntl.flock(handle, fcntl.LOCK_EX)
        try: yield
        finally:
            if os.name == 'nt':
                handle.seek(0);msvcrt.locking(handle.fileno(), msvcrt.LK_UNLCK, 1)
            else: fcntl.flock(handle, fcntl.LOCK_UN)


def settings(path):
    result = {}
    try:
        for line in Path(path).read_text(errors='replace').splitlines():
            match = re.match(r'\s*([a-z_]+)\s*=\s*(.*?)\s*(?:#.*)?$', line)
            if match: result[match[1]] = match[2].strip('"')
    except OSError: pass
    return result


def retro_home():
    return Path(os.environ.get('FLXTR_RETROARCH_HOME', '/home/apps/retroarch'))


def configured_roots():
    home = retro_home()
    roots = [ROOT / 'states', home / 'states', home / '.home/.config/retroarch/states']
    for config in (home / '.home/.config/retroarch/retroarch.cfg', ROOT / 'game-runtime.cfg'):
        value = settings(config).get('savestate_directory', '')
        if value and value != 'default':
            path = Path(os.path.expanduser(value))
            if not path.is_absolute(): path = home / path
            if path != Path(path.anchor): roots.append(path)
    return roots


def walk_states(roots):
    seen = set()
    for root in roots:
        root = root.absolute()
        if not root.is_dir(): continue
        for parent, dirs, files in os.walk(root, followlinks=False):
            dirs[:] = [d for d in dirs if d not in ('state-shadows', '.git', 'music-cache')]
            for name in files:
                path = Path(parent) / name
                if STATE.fullmatch(name) and not path.is_symlink():
                    canonical = str(path.resolve())
                    if canonical not in seen:
                        seen.add(canonical);yield Path(canonical)


def all_sources():
    roots = configured_roots() + [ROOT / 'roms', retro_home() / 'roms', Path('/mnt/disk/roms')]
    try:
        roots += [Path(g['rom']).parent for g in json.loads((ROOT / 'games.json').read_text())[:2048]]
    except (OSError, ValueError, KeyError): pass
    return walk_states(roots)


def sources_for(rom):
    rom = Path(rom).absolute()
    names = re.compile(re.escape(rom.stem) + r'\.state(?:\d+|\.auto)?$', re.I)
    for path in walk_states([rom.parent] + configured_roots()):
        if names.fullmatch(path.name): yield path


def source_id(path):
    return hashlib.sha256(str(Path(path).absolute()).encode()).hexdigest()[:16]


def stamp(path):
    s = path.stat()
    if not stat.S_ISREG(s.st_mode): raise ValueError('State is not a regular file')
    return [s.st_size, s.st_mtime_ns, s.st_ctime_ns, s.st_ino]


def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as source:
        for block in iter(lambda: source.read(1024 * 1024), b''): h.update(block)
    return h.hexdigest()


def record_for(path):
    folder = ROOT / 'state-shadows' / source_id(path)
    try: record = json.loads((folder / 'record.json').read_text())
    except FileNotFoundError: record = {'source': str(Path(path).absolute())}
    except (OSError, ValueError) as error:
        raise ValueError('Shadow index is damaged; existing copies were preserved') from error
    if record['source'] != str(Path(path).absolute()): raise ValueError('Shadow identity collision')
    return folder, record


def capture(path, displaced=False, verify=False):
    path = Path(path).absolute()
    if path.is_symlink(): raise ValueError('Refusing a symlinked savestate')
    before = stamp(path)
    if before[0] > LIMIT: raise ValueError('State exceeds the 512 MB copy limit')
    folder, record = record_for(path)
    old_hashes = {v['hash'] for field in ('current', 'previous', 'suspect', 'baseline') if (v := record.get(field))}
    old_hashes.update(v['hash'] for v in record.get('displaced', []))
    if not displaced and record.get('stamp') == before:
        version = record.get('suspect') or record.get('current')
        observed = record.get('observed_hash') or (version['hash'] if version else None)
        blob = folder / (observed + '.state') if observed else None
        if blob and blob.is_file() and blob.stat().st_size == before[0]:
            if not verify or digest(blob) == observed: return False
    folder.mkdir(parents=True, exist_ok=True)
    h = hashlib.sha256()
    with tempfile.NamedTemporaryFile(dir=folder, delete=False) as out:
        temp = Path(out.name)
        try:
            with path.open('rb') as source:
                for block in iter(lambda: source.read(1024 * 1024), b''):
                    h.update(block);out.write(block)
            out.flush();os.fsync(out.fileno())
            if stamp(path) != before: raise ValueError('State changed during copy; retry after saving finishes')
        except BaseException:
            temp.unlink(missing_ok=True)
            raise
    value = h.hexdigest()
    blob = folder / (value + '.state')
    if blob.exists():
        if digest(blob) != value: os.replace(temp, blob)
        else: temp.unlink()
    else: os.replace(temp, blob)
    version = {'hash': value, 'size': before[0], 'time': int(time.time())}
    record.setdefault('baseline', record.get('current') or version)
    if displaced:
        old = record.setdefault('displaced', [])
        if not any(v['hash'] == value for v in old): old.insert(0, version)
    else:
        current = record.get('current')
        # The installed 1.7.6 has produced raw 4096-byte truncated autosaves.
        # Preserve that candidate too, but do not replace the larger shadow.
        suspicious = current and current['size'] > 8192 and before[0] <= 4096
        if suspicious:
            record['suspect'] = version
        elif not current or current['hash'] != value:
            if current: record['previous'] = current
            record['current'] = version
            record.pop('suspect', None)
        record['stamp'] = before
        record['observed_hash'] = value
    atomic_json(folder / 'record.json', record)
    keep = {v['hash'] for field in ('current', 'previous', 'suspect', 'baseline') if (v := record.get(field))}
    keep.update(v['hash'] for v in record.get('displaced', []))
    # Prune only our obsolete immutable blobs, after committing the manifest.
    for obsolete in old_hashes - keep:
        (folder / (obsolete + '.state')).unlink(missing_ok=True)
    return True


def snapshot(paths, strict=False, verify=False):
    changed = 0
    for path in paths:
        try:
            with locked(): changed += bool(capture(path, verify=verify))
        except (OSError, ValueError) as error:
            if strict: raise
            print('Shadow copy deferred:', path.name, str(error), file=sys.stderr)
    return changed


def emulator_running():
    if os.name == 'nt':
        output = subprocess.check_output(['tasklist', '/fo', 'csv', '/nh'], text=True)
        return any(row and Path(row[0]).name.lower().startswith('retroarch') for row in csv.reader(output.splitlines()))
    if not Path('/proc/self/comm').exists(): raise ValueError('Cannot verify that the emulator is stopped')
    for path in Path('/proc').glob('[0-9]*/comm'):
        try: name = path.read_text().strip().lower()
        except FileNotFoundError: continue
        if name.startswith('retroarch'): return True
    return False


def versions(rom):
    allowed = {str(p.absolute()) for p in sources_for(rom)}
    # Include removed primary files, but only in this ROM's configured locations.
    roots = [Path(rom).absolute().parent] + [p.absolute() for p in configured_roots()]
    for meta in sorted((ROOT / 'state-shadows').glob('*/record.json')):
        try: record = json.loads(meta.read_text())
        except (OSError, ValueError): continue
        source = Path(record.get('source', ''))
        if not re.fullmatch(re.escape(Path(rom).stem) + r'\.state(?:\d+|\.auto)?', source.name, re.I): continue
        if str(source) not in allowed and not any(source.is_relative_to(p) for p in roots): continue
        if not STATE.fullmatch(source.name): continue
        seen = set()
        for tag, field in (('c', 'current'), ('p', 'previous'), ('s', 'suspect'), ('b', 'baseline')):
            if field in record and record[field]['hash'] not in seen:
                seen.add(record[field]['hash'])
                yield meta.parent.name + tag, source, record[field], field
        for i, version in enumerate(record.get('displaced', [])[:10]):
            if version['hash'] not in seen:
                seen.add(version['hash'])
                yield meta.parent.name + 'd' + str(i), source, version, 'before recovery'


def recover(rom, token):
    if emulator_running(): raise ValueError('Exit the game before recovering a savestate')
    with locked():
        match = next((v for v in versions(rom) if v[0] == token), None)
        if not match: raise ValueError('Shadow copy no longer exists')
        _, source, version, _ = match
        folder = ROOT / 'state-shadows' / source_id(source)
        blob = folder / (version['hash'] + '.state')
        if not blob.is_file() or digest(blob) != version['hash']:
            raise ValueError('Shadow checksum failed; choose a different shadow copy')
        if source.is_symlink(): raise ValueError('Refusing a symlinked recovery target')
        mode = stat.S_IMODE(source.stat().st_mode) if source.exists() else 0o644
        if source.exists(): capture(source, displaced=True)
        source.parent.mkdir(parents=True, exist_ok=True)
        with tempfile.NamedTemporaryFile(dir=source.parent, prefix='.flxtr-recover-', delete=False) as out:
            temp = Path(out.name)
            try:
                os.chmod(temp, mode)
                with blob.open('rb') as data: shutil.copyfileobj(data, out, 1024 * 1024)
                out.flush();os.fsync(out.fileno())
                if digest(temp) != version['hash']: raise ValueError('Recovery copy verification failed')
                if emulator_running(): raise ValueError('Game started during recovery; recovery cancelled')
                os.replace(temp, source);sync_dir(source.parent)
            except BaseException:
                temp.unlink(missing_ok=True);raise
        capture(source)


def slot_for(key):
    value = settings(retro_home() / '.home/.config/retroarch/retroarch.cfg').get('state_slot', '0')
    try: slot = max(0, min(99, int(value)))
    except ValueError: slot = 0
    try:
        for line in (ROOT / 'rom-state-slots.tsv').read_text().splitlines():
            fields = line.split('\t')
            if len(fields) == 2 and fields[0] == key and fields[1].isdigit() and 0 <= int(fields[1]) <= 99:
                slot = int(fields[1])
    except OSError: pass
    return slot


def set_slot(key, delta):
    if not re.fullmatch('[0-9a-f]{16}', key): raise ValueError('Invalid ROM identity')
    path = ROOT / 'rom-state-slots.tsv'
    with locked():
        try: lines = [line for line in path.read_text().splitlines() if line.split('\t')[0] != key]
        except OSError: lines = []
        lines.append(f'{key}\t{(slot_for(key) + int(delta)) % 100}')
        temp = path.with_suffix('.tsv.next')
        with temp.open('w') as out:
            out.write('\n'.join(lines) + '\n');out.flush();os.fsync(out.fileno())
        os.replace(temp, path);sync_dir(path.parent)


def session():
    data = json.loads((ROOT / 'state-session.json').read_text())
    if data.get('home'): os.environ['FLXTR_RETROARCH_HOME'] = data['home']
    return data


def alive(pid):
    try: os.kill(pid, 0);return True
    except ProcessLookupError: return False


def watch(pid):
    began = time.monotonic()
    while not (ROOT / 'state-session.json').exists():
        if not alive(pid) or time.monotonic() - began > 30: return
        time.sleep(0.1)
    rom = session()['rom']
    observed = {}
    while alive(pid):
        for path in sources_for(rom):
            try:
                current = stamp(path)
                if observed.get(str(path)) == current: snapshot([path])
                observed[str(path)] = current
            except OSError: pass
        time.sleep(1)
    # Finish after RetroArch has exited; never rely on its in-flight buffers.
    snapshot(sources_for(rom));time.sleep(0.25);snapshot(sources_for(rom))


def run(args):
    if args == ['prepare']:
        snapshot(sources_for(session()['rom']), strict=True)
    elif args == ['seed']:
        print('Savestate shadows captured:', snapshot(all_sources()))
    elif len(args) == 2 and args[0] == 'watch':
        watch(int(args[1]))
    else: raise ValueError('Expected seed, prepare or watch PID')


if __name__ == '__main__':
    try: run(sys.argv[1:])
    except Exception as error:
        print('Savestate protection:', error, file=sys.stderr);sys.exit(1)

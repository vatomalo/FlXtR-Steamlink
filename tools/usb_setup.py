#!/usr/bin/env python3
"""Steam Link USB setup. scan/plan are read-only; execute requires a fresh plan.

The formatter never accepts a raw device argument: it consumes the exact disk
identity captured in a short-lived plan, plus its explicit confirmation phrase.
"""
import argparse
import fcntl
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import stat
import struct
import subprocess
import sys
import tempfile
import time
import uuid

GIB = 1024 ** 3
STATE = Path('/mnt/config/flxtr-usb')
HOOK = Path('/etc/init.d/startup/S19zz_flxtr_usb.sh')
TEMPLATE = Path(__file__).with_name('usb_mount_template.sh')
CRITICAL = {'/', '/home/apps', '/usr/local', '/mnt/config', '/mnt/scratch'}


def clean(value, maximum=150):
    return re.sub(r'[\x00-\x1f\x7f]', ' ', str(value)).strip()[:maximum]


def atomic(path, value):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + '.new')
    with temporary.open('w') as stream:
        stream.write(value)
        stream.flush()
        os.fsync(stream.fileno())
    temporary.replace(path)


def mount_records(text=None):
    records = []
    for line in (Path('/proc/self/mountinfo').read_text() if text is None else text).splitlines():
        fields = line.split()
        split = fields.index('-')
        decode = lambda s: re.sub(r'\\([0-7]{3})', lambda m: chr(int(m[1], 8)), s)
        records.append({'dev': fields[2], 'root': decode(fields[3]), 'path': decode(fields[4]),
                        'type': fields[split + 1], 'source': decode(fields[split + 2])})
    return records


def filesystem(path):
    """Read superblocks without mounting or replaying a filesystem journal."""
    with open(path, 'rb', buffering=0) as stream:
        head = stream.read(512)
        stream.seek(1024)
        sb = stream.read(1024)
    if len(head) == 512 and head[82:90].startswith(b'FAT32'):
        serial = struct.unpack_from('<I', head, 67)[0]
        return {'type': 'vfat', 'uuid': '%04X-%04X' % (serial >> 16, serial & 65535)}
    if len(sb) == 1024 and sb[56:58] == b'\x53\xef':
        incompat = struct.unpack_from('<I', sb, 96)[0]
        compat = struct.unpack_from('<I', sb, 92)[0]
        kind = 'ext4' if incompat & 0x40 else 'ext3' if compat & 4 else 'ext2'
        return {'type': kind, 'uuid': str(uuid.UUID(bytes=sb[104:120]))}
    return {'type': 'unknown', 'uuid': ''}


def inventory(app):
    mounts = mount_records()
    protected = {m['dev'] for m in mounts if m['path'] in CRITICAL}
    for folder in (Path(app), Path(__file__).resolve().parent):
        info = folder.stat()
        protected.add('%d:%d' % (os.major(info.st_dev), os.minor(info.st_dev)))
    disks = []
    for entry in sorted(Path('/sys/class/block').glob('sd*')):
        if not re.fullmatch(r'sd[a-z]+', entry.name) or (entry / 'partition').exists():
            continue
        real = entry.resolve()
        usb = next((p for p in real.parents if (p / 'idVendor').exists() and (p / 'idProduct').exists()), None)
        if usb is None:
            continue
        def read(path, default=''):
            try:
                return Path(path).read_text().strip()
            except OSError:
                return default
        device = '/dev/block/' + entry.name
        if not Path(device).exists():
            device = '/dev/' + entry.name
        info = os.stat(device)
        if not stat.S_ISBLK(info.st_mode):
            continue
        with open(device, 'rb', buffering=0) as stream:
            head_hash = hashlib.sha256(stream.read(65536)).hexdigest()
        parts = []
        for part in sorted(entry.glob(entry.name + '*')):
            if not (part / 'partition').exists():
                continue
            node = str(Path(device).with_name(part.name))
            fs = filesystem(node)
            dev = read(part / 'dev')
            parts.append(dict(fs, device=node, dev=dev, size=int(read(part / 'size', '0')) * 512,
                              mounts=[m['path'] for m in mounts if m['dev'] == dev]))
        key = {'device': device, 'sysfs': str(real), 'dev': read(entry / 'dev'),
               'size': int(read(entry / 'size', '0')) * 512,
               'sector': int(read(entry / 'queue/logical_block_size', '512')),
               'model': clean(read(entry / 'device/model') or read(usb / 'product')),
               'serial': clean(read(usb / 'serial')), 'port': str(usb),
               'generation': read(usb / 'devnum'), 'vendor': read(usb / 'idVendor'),
               'product': read(usb / 'idProduct'), 'head_hash': head_hash}
        blocked = any(p['dev'] in protected for p in parts) or key['dev'] in protected
        disks.append(dict(key, token=hashlib.sha256(json.dumps(key, sort_keys=True).encode()).hexdigest(),
                          partitions=parts, blocked=blocked,
                          reason='Contains running apps or system libraries' if blocked else ''))
    return disks


def layout(size, apps_gib=None):
    if size < 4 * GIB or size >= 2 * 1024 * GIB:
        raise ValueError('Supported drive size: 4 GiB to less than 2 TiB')
    apps = int(apps_gib * GIB) if apps_gib else min(8 * GIB, max(GIB, size // 4))
    apps = apps // (1024 * 1024) * (1024 * 1024)
    total = size // 512
    end = (total - 2048) // 2048 * 2048
    linux_start = (end - apps // 512) // 2048 * 2048
    if apps < GIB or linux_start - 2048 < 2 * GIB // 512:
        raise ValueError('Keep at least 1 GiB for apps and 2 GiB for FAT data')
    return {'data_start': 2048, 'data_sectors': linux_start - 2048,
            'apps_start': linux_start, 'apps_sectors': end - linux_start}


def mbr(partitions):
    result = bytearray(512)
    result[440:444] = os.urandom(4)
    for offset, kind, start, length in [(446, 0x0c, partitions['data_start'], partitions['data_sectors']),
                                         (462, 0x83, partitions['apps_start'], partitions['apps_sectors'])]:
        result[offset:offset + 16] = struct.pack('<B3sB3sII', 0, b'\xfe\xff\xff', kind,
                                                b'\xfe\xff\xff', start, length)
    result[510:512] = b'\x55\xaa'
    return bytes(result)


def source_bytes(paths):
    total = 0
    for root in paths:
        for directory, subdirs, files in os.walk(root, followlinks=False):
            for name in files:
                info = (Path(directory) / name).lstat()
                if stat.S_ISREG(info.st_mode):
                    total += info.st_size
    return total


def create_plan(device, action, app, apps_gib=None):
    app = str(Path(app).resolve())
    disks = inventory(app)
    selected = next((d for d in disks if d['device'] == device), None)
    if selected is None:
        raise ValueError('Selected USB drive is no longer connected')
    if action == 'prepare' and selected['blocked']:
        raise ValueError('Refusing to erase the drive containing running apps or system libraries')
    if selected['sector'] != 512:
        raise ValueError('This setup supports USB drives with 512-byte logical sectors')
    plan = {'version': 1, 'created': time.time(), 'created_monotonic': time.monotonic(),
            'boot_id': Path('/proc/sys/kernel/random/boot_id').read_text().strip(), 'state': 'review', 'action': action,
            'disk': selected, 'app': app, 'nonce': os.urandom(4).hex().upper(),
            'template_hash': hashlib.sha256(TEMPLATE.read_bytes()).hexdigest()}
    # Do not silently switch away from an existing active USB installation.
    plan['activate'] = not any(d['blocked'] and d['device'] != device for d in disks)
    if action == 'restore':
        data = [p for p in selected['partitions'] if p['type'] == 'vfat']
        system = [p for p in selected['partitions'] if p['type'] in ('ext3', 'ext4')]
        if len(data) != 1 or len(system) != 1:
            raise ValueError('Use an existing drive with one FAT32 and one Linux apps partition')
        if not plan['activate']:
            raise ValueError('Another USB provides the active installation. Repair that drive or start FlXtR from internal storage.')
        plan['data'], plan['system'] = data[0], system[0]
    else:
        for tool in ['/bin/mkfs.vfat', '/usr/sbin/mkfs.ext4']:
            if not os.access(tool, os.X_OK):
                raise ValueError('Missing required filesystem tool: ' + tool)
        plan['layout'] = layout(selected['size'], apps_gib)
        needed = source_bytes(['/home/apps', '/usr/local'])
        if needed + max(256 * 1024 * 1024, needed // 5) > plan['layout']['apps_sectors'] * 512:
            raise ValueError('Apps/libraries will not fit. Choose a larger apps partition with --apps-gib.')
        plan['copy_bytes'] = needed
    plan['confirmation'] = ('ERASE-' if action == 'prepare' else 'RESTORE-') + plan['nonce']
    directions = ['UP', 'DOWN', 'LEFT', 'RIGHT']
    import random
    random.SystemRandom().shuffle(directions)
    plan['directions'] = directions
    return plan


def revalidate(plan, check_head=True):
    if plan.get('version') != 1 or plan.get('state') != 'review' or not 0 <= time.monotonic() - plan['created_monotonic'] <= 300:
        raise ValueError('Plan expired or already used. Scan the drives again.')
    if plan['boot_id'] != Path('/proc/sys/kernel/random/boot_id').read_text().strip():
        raise ValueError('Device rebooted after review. Create a fresh plan.')
    if plan['template_hash'] != hashlib.sha256(TEMPLATE.read_bytes()).hexdigest():
        raise ValueError('Setup software changed after review. Create a new plan.')
    disks = inventory(plan['app'])
    current = next((d for d in disks if d['device'] == plan['disk']['device']), None)
    identity = ('sysfs', 'dev', 'size', 'sector', 'model', 'serial', 'port', 'generation', 'vendor', 'product', 'head_hash')
    if current is None or current['token'] != plan['disk']['token'] or any(current[k] != plan['disk'][k] for k in identity):
        raise ValueError('USB identity or partition table changed. No erasure performed; scan again.')
    if plan['action'] == 'prepare' and current['blocked']:
        raise ValueError('The selected USB now contains active apps or libraries')
    if plan['activate'] and any(d['blocked'] and d['device'] != current['device'] for d in disks):
        raise ValueError('Another USB became the active installation after review')
    if plan['action'] == 'prepare':
        shape = plan['layout']
        if shape != layout(current['size'], shape['apps_sectors'] * 512 / GIB):
            raise ValueError('Partition layout differs from the reviewed drive geometry')
    else:
        for part in (plan['data'], plan['system']):
            if not any(all(p[k] == part[k] for k in ('device', 'type', 'uuid')) for p in current['partitions']):
                raise ValueError('Filesystem identity changed after review')
    return current


def platform_guard():
    if os.geteuid() != 0 or not Path('/dev/block/mtdblock9').exists():
        raise ValueError('Execution is restricted to a root session on Steam Link hardware')
    if 'MV88DE3108' not in Path('/proc/cpuinfo').read_text():
        raise ValueError('This is not the supported Steam Link hardware')


def run(args):
    subprocess.run(args, check=True, stdin=subprocess.DEVNULL, stdout=sys.stderr, stderr=sys.stderr)


def mounted_root(device):
    for record in reversed(mount_records()):
        if record['source'] == device and record['root'] == '/':
            info = Path(record['path']).stat()
            visible = '%d:%d' % (os.major(info.st_dev), os.minor(info.st_dev))
            if visible == record['dev']:
                return Path(record['path'])
    return None


def mount_volume(device, kind, target):
    target.mkdir(parents=True, exist_ok=True)
    previous = mounted_root(device)
    run(['/bin/mount', '--bind', str(previous), str(target)] if previous else
        ['/bin/mount', '-t', kind, device, str(target)])


def seed_tree(source, destination):
    """Copy to a NEW apps filesystem; preserve source data and symbolic links."""
    destination.mkdir(parents=True, exist_ok=True)
    for item in Path(source).iterdir():
        print('Copying ' + str(item), flush=True)
        target = destination / item.name
        if item.is_symlink():
            target.symlink_to(os.readlink(item))
        elif item.is_dir():
            shutil.copytree(item, target, symlinks=True, ignore=lambda folder, names:
                            [n for n in names if not ((Path(folder) / n).is_symlink() or
                             stat.S_ISREG((Path(folder) / n).lstat().st_mode) or (Path(folder) / n).is_dir())])
        elif item.is_file():
            shutil.copy2(item, target)


def format_drive(plan):
    disk = plan['disk']
    # All bind mounts must be gone before partition-table writes. No force/lazy
    # unmount is ever used, so busy files/loop devices cause a safe refusal.
    parts = {p['dev'] for p in disk['partitions']}
    mounted = [m for m in mount_records() if m['dev'] in parts]
    if any(m['path'] in CRITICAL for m in mounted):
        raise ValueError('A selected partition is now used by the system')
    for entry in sorted(mounted, key=lambda x: len(x['path']), reverse=True):
        run(['/bin/umount', entry['path']])
    revalidate(plan)
    node = disk['device']
    descriptor = os.open(node, os.O_RDWR | os.O_EXCL)
    try:
        opened = os.fstat(descriptor)
        if not stat.S_ISBLK(opened.st_mode):
            raise ValueError('Target is not a physical block device')
        if '%d:%d' % (os.major(opened.st_rdev), os.minor(opened.st_rdev)) != disk['dev']:
            raise ValueError('Opened block device differs from the selected USB')
        revalidate(plan)
        if hashlib.sha256(os.pread(descriptor, 65536, 0)).hexdigest() != disk['head_hash']:
            raise ValueError('Opened USB metadata differs from the reviewed drive')
        table = mbr(plan['layout'])
        # Clear old primary/backup partition metadata on THIS disk only.
        zeros = bytes(1024 * 1024)
        def write_all(data, offset):
            done = 0
            while done < len(data):
                amount = os.pwrite(descriptor, data[done:], offset + done)
                if amount <= 0:
                    raise OSError('Short write to selected USB')
                done += amount
        write_all(zeros, 0)
        write_all(zeros, disk['size'] - len(zeros))
        write_all(table, 0)
        os.fsync(descriptor)
        fcntl.ioctl(descriptor, 0x125f)  # BLKRRPART
    finally:
        os.close(descriptor)
    for _ in range(40):
        if Path(node + '1').exists() and Path(node + '2').exists():
            break
        time.sleep(.25)
    # Recheck USB generation and exact partition geometry before each formatter.
    def check():
        fresh = next((d for d in inventory(plan['app']) if d['device'] == node), None)
        if fresh is None or any(fresh[k] != disk[k] for k in ('sysfs', 'dev', 'serial', 'generation', 'size')):
            raise ValueError('USB was disconnected or replaced; formatting stopped')
        for number, prefix in ((1, 'data'), (2, 'apps')):
            sysfs = Path('/sys/class/block') / (Path(node).name + str(number))
            if int((sysfs / 'start').read_text()) != plan['layout'][prefix + '_start'] or int((sysfs / 'size').read_text()) != plan['layout'][prefix + '_sectors']:
                raise ValueError('Kernel partition geometry differs from the reviewed plan')
        with open(node, 'rb', buffering=0) as stream:
            if stream.read(512) != table:
                raise ValueError('Partition table changed while preparing USB')
    check()
    run(['/bin/mkfs.vfat', '-n', 'FLXTR_DATA', node + '1'])
    check()
    run(['/usr/sbin/mkfs.ext4', '-t', 'ext3', '-F', '-m', '0', '-L', 'FLXTR_APPS', node + '2'])
    data, system = filesystem(node + '1'), filesystem(node + '2')
    if data['type'] != 'vfat' or system['type'] != 'ext3' or not data['uuid'] or not system['uuid']:
        raise ValueError('New filesystems failed verification')
    return dict(data, device=node + '1'), dict(system, device=node + '2')


def configure(data, system, app_name):
    if not re.fullmatch(r'[A-Za-z0-9_-]+', app_name):
        raise ValueError('Unsupported application directory name')
    for part in (data, system):
        if not re.fullmatch(r'[A-Fa-f0-9-]+', part['uuid']):
            raise ValueError('Invalid filesystem UUID')
    script = TEMPLATE.read_text().replace('@FAT_UUID@', data['uuid']).replace('@SYSTEM_UUID@', system['uuid'])
    script = script.replace('@APP_NAME@', app_name)
    text = '#!/bin/sh\n# FlXtR USB storage hook v1\n/bin/sh /mnt/config/flxtr-usb/mounts.sh --apply || echo "FlXtR USB mount unavailable"\nexit 0\n'
    if HOOK.exists() and 'FlXtR USB storage hook v1' not in HOOK.read_text():
        raise ValueError('Refusing to overwrite an unrelated startup hook')
    STATE.mkdir(parents=True, exist_ok=True)
    if len(list(STATE.glob('*.backup-*'))) >= 20:
        raise ValueError('Storage configuration has twenty backups; review them before making more changes')
    target = STATE / 'mounts.sh'
    run_candidate = STATE / 'mounts.candidate.sh'
    atomic(run_candidate, script)
    run(['/bin/sh', '-n', str(run_candidate)])
    # Existing stock and recovery hooks are left intact. Back up only our files.
    for old in (target, STATE / 'storage.json'):
        if old.exists():
            shutil.copy2(old, old.with_name(old.name + '.backup-' + str(time.time_ns())))
    # Apply the checked candidate before making it persistent at boot.
    run(['/bin/sh', str(run_candidate), '--apply'])
    run_candidate.replace(target)
    target.chmod(0o755)
    atomic(HOOK, text)
    HOOK.chmod(0o755)
    atomic(STATE / 'storage.json', json.dumps({'version': 1, 'data_uuid': data['uuid'], 'system_uuid': system['uuid']}, indent=2))


def execute(plan_path, confirmation):
    platform_guard()
    plan_path = Path(plan_path)
    plan = json.loads(plan_path.read_text())
    if confirmation != plan.get('confirmation'):
        raise ValueError('Confirmation does not match this reviewed drive')
    with open('/tmp/flxtr-usb-setup.lock', 'a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        revalidate(plan)
        atomic(plan_path, json.dumps(dict(plan, state='executing')))
        work = Path(tempfile.mkdtemp(prefix='flxtr-usb-setup-'))
        data_mount, system_mount = work / 'data', work / 'system'
        mounted = []
        try:
            if plan['action'] == 'prepare':
                print('Creating selected USB filesystems. Do not remove the drive.', flush=True)
                data, system = format_drive(plan)
            else:
                data, system = plan['data'], plan['system']
            mount_volume(data['device'], data['type'], data_mount); mounted.append(data_mount)
            mount_volume(system['device'], system['type'], system_mount); mounted.append(system_mount)
            if plan['action'] == 'prepare':
                print('Copying apps and local libraries; source installation is preserved.', flush=True)
                seed_tree('/home/apps', system_mount / 'apps')
                seed_tree('/usr/local', system_mount / 'local')
                for folder in ('.system', 'roms', 'videos', 'flxtr-backups'):
                    (data_mount / folder).mkdir(exist_ok=True)
                marker = json.dumps({'version': 1, 'data_uuid': data['uuid'], 'system_uuid': system['uuid']})
                atomic(data_mount / '.flxtr-storage.json', marker)
                atomic(system_mount / '.flxtr-storage.json', marker)
                os.sync()
            if not (system_mount / 'apps').is_dir() or not (system_mount / 'local').is_dir() or (system_mount / 'apps').is_symlink() or (system_mount / 'local').is_symlink():
                raise ValueError('Linux partition does not contain apps and local directories')
            if not (data_mount / '.system').is_dir():
                (data_mount / '.system').mkdir()
            if plan['activate']:
                print('Restoring UUID mounts and stable ROM links.', flush=True)
                configure(data, system, Path(plan['app']).name)
            atomic(plan_path, json.dumps(dict(plan, state='done')))
            print('DONE: ' + ('Mounts and links are configured. Close and reopen FlXtR.' if plan['activate'] else
                            'Spare USB prepared. Current primary USB mappings were preserved.'), flush=True)
        finally:
            for mount in reversed(mounted):
                subprocess.run(['/bin/umount', str(mount)], stdout=sys.stderr, stderr=sys.stderr)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--app', default=str(Path(__file__).resolve().parent.parent))
    sub = parser.add_subparsers(dest='command', required=True)
    scan = sub.add_parser('scan'); scan.add_argument('--tsv', action='store_true')
    plan = sub.add_parser('plan'); plan.add_argument('--device', required=True)
    plan.add_argument('--action', choices=['restore', 'prepare'], required=True)
    plan.add_argument('--apps-gib', type=int); plan.add_argument('--out', required=True)
    apply = sub.add_parser('execute'); apply.add_argument('--plan', required=True); apply.add_argument('--confirm', required=True)
    args = parser.parse_args()
    if args.command == 'scan':
        disks = inventory(args.app)
        if args.tsv:
            for d in disks:
                print('\t'.join([d['device'], clean(d['model']), '%.1f GiB' % (d['size'] / GIB), clean(d['serial'] or d['port']),
                                 d['token'], 'LOCKED' if d['blocked'] else 'AVAILABLE', clean(d['reason']), clean(d['port'])]))
        else:
            print(json.dumps(disks, indent=2))
    elif args.command == 'plan':
        value = create_plan(args.device, args.action, args.app, args.apps_gib)
        atomic(args.out, json.dumps(value, indent=2)); os.chmod(args.out, 0o600)
        disk = value['disk']
        fields = {'device': disk['device'], 'model': disk['model'], 'size': '%.1f GiB' % (disk['size'] / GIB),
                  'identity': disk['token'],
                  'serial': disk['serial'] or disk['port'], 'confirmation': value['confirmation'],
                  'directions': ' '.join(value['directions']), 'activate': str(int(value['activate']))}
        if args.action == 'prepare':
            fields['layout'] = 'FAT32 data %.1f GiB / Linux apps %.1f GiB' % (value['layout']['data_sectors'] * 512 / GIB, value['layout']['apps_sectors'] * 512 / GIB)
        else:
            fields['layout'] = 'Use existing FAT32 + Linux; no formatting'
        for key, value in fields.items():
            print(key + '\t' + clean(value))
    else:
        execute(args.plan, args.confirm)


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, KeyError, TypeError, subprocess.SubprocessError) as error:
        print('USB setup stopped: ' + clean(error, 500), file=sys.stderr)
        sys.exit(1)

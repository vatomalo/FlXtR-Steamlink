#!/usr/bin/env python3
"""Per-ROM artwork and explicit Libretro cheat downloads. No game/save edits."""
import difflib
import hashlib
import json
import os
from pathlib import Path
import re
import ssl
import subprocess
import sys
import time
import urllib.parse
import urllib.request
sys.path.insert(0, str(Path(__file__).resolve().parent))
import state_guard

ROOT = Path('.')
MAX_CHEATS = 512
SYSTEMS = ['Nintendo - Nintendo Entertainment System',
           'Nintendo - Super Nintendo Entertainment System',
           'Nintendo - Game Boy Advance', 'Nintendo - Game Boy',
           'Sega - Mega Drive - Genesis', 'Sony - PlayStation',
           'Sony - PlayStation Portable', 'FBNeo - Arcade Games', 'FBNeo - Arcade Games']


def identity(path):
    value = 14695981039346656037
    for byte in path.encode():
        value = ((value ^ byte) * 1099511628211) & ((1 << 64) - 1)
    return f'{value:016x}'


def key_valid(key):
    return bool(re.fullmatch('[0-9a-f]{16}', key))


def clean(value, limit=190):
    return re.sub(r'[\x00-\x1f\x7f]', ' ', str(value))[:limit]


def atomic(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    temp = path.with_suffix(path.suffix + '.next')
    temp.write_text(value, encoding='utf-8')
    os.replace(temp, path)


def rows(values):
    atomic(ROOT / 'rom-assets.tsv', ''.join('\t'.join(clean(x) for x in row) + '\n' for row in values))


def game(key):
    if not key_valid(key):
        raise ValueError('Invalid ROM identity')
    for item in json.loads((ROOT / 'games.json').read_text())[:2048]:
        if identity(item['rom']) == key:
            return item
    raise ValueError('ROM no longer exists in the library')


def cover_index():
    worker = (ROOT / ('greenlink-catalog.exe' if os.name == 'nt' else 'greenlink-catalog')).resolve()
    result = subprocess.run([str(worker), '--game-cover-index'], cwd=ROOT, check=True,
                            capture_output=True, text=True, timeout=30)
    result_rows = []
    for line in result.stdout.splitlines()[:2048]:
        fields = line.split('\t')
        if len(fields) == 3 and key_valid(fields[0]) and cover_valid(fields[2]):
            result_rows.append(fields)
    return result_rows


def cover_valid(value):
    return (len(value) < 512 and '..' not in value and not re.search(r'[\x00-\x1f]', value)
            and value.startswith(('gameart:', 'localthumb:', 'game-art-cache/')))


def covers(key, donor=None):
    game(key)
    index = cover_index()
    path = ROOT / 'rom-covers.tsv'
    current = 'automatic'
    if path.exists():
        for line in path.read_text().splitlines():
            fields = line.split('\t')
            if len(fields) == 4 and fields[0] == key:
                current = fields[1]
    if donor is not None:
        current = donor
        saved = []
        if path.exists():
            saved = [line for line in path.read_text().splitlines()
                     if line.split('\t', 1)[0] != key]
        if donor != 'automatic':
            selected = next((r for r in index if r[0] == donor), None)
            if not selected:
                raise ValueError('Cover source is no longer in the library')
            saved.append('\t'.join([key, selected[0], clean(selected[1], 79), selected[2]]))
        atomic(path, '\n'.join(saved) + ('\n' if saved else ''))
    rows([('automatic', 'AUTOMATIC COVER', 'CURRENT' if current == 'automatic' else 'RESET TO ORIGINAL ARTWORK')]
         + [(k, name, 'CURRENT' if current == k else 'USE THIS GAME COVER') for k, name, _ in sorted(index, key=lambda r: r[1].casefold()) if k != key])


def system_for(item):
    path = item['rom'].lower()
    ext = Path(path).suffix
    if ext == '.nes': return SYSTEMS[0]
    if ext in ('.sfc', '.smc'): return SYSTEMS[1]
    if ext == '.gba': return SYSTEMS[2]
    if ext == '.gbc': return 'Nintendo - Game Boy Color'
    if ext == '.gb': return SYSTEMS[3]
    if ext == '.sms': return 'Sega - Master System - Mark III'
    if ext == '.gg': return 'Sega - Game Gear'
    if ext in ('.md', '.gen', '.smd'): return SYSTEMS[4]
    if ext == '.cso' or ('/psp/' in path or '/pspiso/' in path) and ext in ('.iso', '.pbp'):
        return SYSTEMS[6]
    if ext in ('.cue', '.chd', '.m3u', '.iso', '.pbp'): return SYSTEMS[5]
    if '/neogeo/' in path or '/neo-geo/' in path: return SYSTEMS[8]
    if '/mame/' in path or '/arcade/' in path: return SYSTEMS[7]
    raise ValueError('No Libretro cheat system is known for this ROM')


def fetch(url, limit=4_000_000):
    allowed = ('https://api.github.com/repos/libretro/libretro-database/',
               'https://raw.githubusercontent.com/libretro/libretro-database/master/cht/')
    if not url.startswith(allowed):
        raise ValueError('Only the official Libretro cheat database is supported')
    request = urllib.request.Request(url, headers={'User-Agent': 'FlXtR-ROM-cheats', 'Accept': 'application/vnd.github+json'})
    context = ssl.create_default_context(cafile=os.environ.get('SSL_CERT_FILE'))
    with urllib.request.urlopen(request, context=context, timeout=20) as response:
        data = response.read(limit + 1)
    if len(data) > limit:
        raise ValueError('Cheat database response exceeds the size limit')
    return data.decode('utf-8-sig')


def cheat_files(system):
    cache = ROOT / 'cheats/index' / (hashlib.sha256(system.encode()).hexdigest() + '.json')
    try:
        if time.time() - cache.stat().st_mtime < 86400:
            return json.loads(cache.read_text())
    except (OSError, ValueError):
        pass
    # Git trees include systems with more than the Contents API's 1000 entries.
    base = 'https://api.github.com/repos/libretro/libretro-database/'
    systems = json.loads(fetch(base + 'contents/cht'))
    entry = next((e for e in systems if e['name'] == system and e['type'] == 'dir'), None)
    if not entry:
        raise ValueError('No official cheat folder for this system')
    tree = json.loads(fetch(base + 'git/trees/' + entry['sha'] + '?recursive=1'))
    if tree.get('truncated'):
        raise ValueError('Cheat index was incomplete; try again later')
    files = [e['path'] for e in tree['tree'] if e['type'] == 'blob' and e['path'].endswith('.cht')
             and '..' not in e['path'].split('/') and not re.search(r'[\x00-\x1f]', e['path'])]
    atomic(cache, json.dumps(files))
    return files


def normalize(title):
    return re.sub('[^a-z0-9]', '', re.sub(r'\([^)]*\)|\[[^]]*\]', '', title.lower()))


def search(key, title=None):
    item = game(key)
    system = system_for(item)
    query = normalize(title or item['title'])
    ranked = sorted(cheat_files(system), key=lambda name: (
        -difflib.SequenceMatcher(None, query, normalize(Path(name).stem)).ratio(), name))[:200]
    atomic(ROOT / f'cheats/{key}.choices.json', json.dumps({'system': system, 'files': ranked}))
    rows([(str(i), Path(name).stem, system) for i, name in enumerate(ranked)]
         or [('none', 'NO CHEATS AVAILABLE', system)])


def parse_cheats(text):
    if len(text.encode()) > 1_000_000:
        raise ValueError('Cheat file is too large')
    fields = {}
    for line in text.splitlines():
        match = re.fullmatch(r'\s*(cheats|cheat\d+_[a-z_]+)\s*=\s*(.*?)\s*', line)
        if match:
            fields[match[1]] = match[2]
    try:
        count = int(fields.get('cheats', '0').strip('"'))
    except ValueError:
        count = 0
    if not 0 < count <= MAX_CHEATS:
        raise ValueError('Unsupported cheat count (maximum 512)')
    # Preserve code strings and memory-handler fields verbatim, discard global
    # settings and out-of-range records. Downloading does not enable any code.
    fields = {k: v for k, v in fields.items() if k == 'cheats' or int(re.match(r'cheat(\d+)_', k)[1]) < count}
    return count, fields


def cheat_text(count, fields):
    return '\n'.join(['cheats = ' + str(count)] + [k + ' = ' + v for k, v in fields.items() if k != 'cheats']) + '\n'


def cheats(key, toggle=None):
    game(key)
    path = ROOT / f'cheats/{key}.cht'
    result = [('download', 'DOWNLOAD / CHANGE CHEAT FILE', 'OFFICIAL LIBRETRO DATABASE')]
    if path.exists():
        count, fields = parse_cheats(path.read_text())
        if toggle is not None:
            i = int(toggle)
            if not 0 <= i < count:
                raise ValueError('Cheat no longer exists')
            field = f'cheat{i}_enable'
            fields[field] = 'false' if fields.get(field, 'false').strip('"').lower() == 'true' else 'true'
            atomic(path, cheat_text(count, fields))
        for i in range(count):
            desc = fields.get(f'cheat{i}_desc', f'Cheat {i + 1}').strip('"').replace('\\', ' ')
            enabled = fields.get(f'cheat{i}_enable', 'false').strip('"').lower() == 'true'
            result.append((str(i), desc, 'ON' if enabled else 'OFF'))
    rows(result)


def install(key, selected):
    game(key)
    choice = json.loads((ROOT / f'cheats/{key}.choices.json').read_text())
    name = choice['files'][int(selected)]
    if '..' in name.split('/') or not name.endswith('.cht'):
        raise ValueError('Invalid cheat file')
    url = 'https://raw.githubusercontent.com/libretro/libretro-database/master/cht/' + urllib.parse.quote(choice['system'] + '/' + name, safe='/')
    count, fields = parse_cheats(fetch(url, 1_000_000))
    for i in range(count):
        fields[f'cheat{i}_enable'] = 'false'
    path = ROOT / f'cheats/{key}.cht'
    if path.exists():
        atomic(path.with_suffix('.cht.previous'), path.read_text())
    atomic(path, cheat_text(count, fields))
    atomic(ROOT / f'cheats/{key}.source.txt', choice['system'] + '\n' + name + '\n')
    cheats(key)


def states(key, delta=None, recovery=None):
    item = game(key)
    state_guard.ROOT = ROOT
    if delta is not None: state_guard.set_slot(key, delta)
    if recovery is not None:
        selected = next((v for v in state_guard.versions(item['rom']) if v[0] == recovery), None)
        state_guard.recover(item['rom'], recovery)
        if selected:
            match = re.search(r'\.state(\d*)$', selected[1].name, re.I)
            if match and int(match[1] or 0) <= 99:
                state_guard.set_slot(key, int(match[1] or 0) - state_guard.slot_for(key))
    if not state_guard.emulator_running(): state_guard.snapshot(state_guard.sources_for(item['rom']), verify=True)
    result = [('slot', f'SAVESTATE SLOT: {state_guard.slot_for(key)}', 'LEFT/RIGHT CHANGE / 0-99'),
              ('backup', 'UPDATE SHADOW COPIES', 'AUTO AND ALL NUMBERED SLOTS')]
    for token, source, version, kind in state_guard.versions(item['rom']):
        size = version['size']
        match = re.search(r'\.state(\d*)$', source.name, re.I)
        slot = str(int(match[1] or 0)) if match else 'AUTO'
        result.append((token, f'{kind.upper()} / SLOT {slot} / {size} B / {source.parent.name}/{source.name}',
                       time.strftime('%Y-%m-%d %H:%M', time.localtime(version['time']))))
    rows(result)


def audio(key, toggle=False):
    game(key)
    path = ROOT / 'rom-audio.tsv'
    saved = path.read_text().splitlines() if path.exists() else []
    enabled = False
    for line in saved:
        fields = line.split('\t')
        if len(fields) == 2 and fields[0] == key and fields[1] in ('0', '1'):
            enabled = fields[1] == '1'
    if toggle:
        enabled = not enabled
        saved = [line for line in saved if line.split('\t', 1)[0] != key]
        saved.append(f'{key}\t{int(enabled)}')
        atomic(path, '\n'.join(saved) + '\n')
    rows([('background', 'BACKGROUND MUSIC: ' + ('ON' if enabled else 'OFF'),
           'START MUSIC BEFORE LAUNCH / EFFECTS UNCHANGED'),
          ('none', 'GAME MUSIC: USE THE GAME SOUND OPTIONS',
           'NO SAFE MUSIC-ONLY MUTE IN INSTALLED CORES')])


def run(args):
    if len(args) not in (2, 3):
        raise ValueError('Expected action, ROM identity and optional selection')
    verb, key = args[:2]
    value = args[2] if len(args) == 3 else None
    if verb == 'audio': audio(key)
    elif verb == 'audio-toggle': audio(key, True)
    elif verb == 'covers': covers(key)
    elif verb == 'cover-set' and value: covers(key, value)
    elif verb == 'cheats': cheats(key)
    elif verb == 'toggle' and value is not None: cheats(key, value)
    elif verb == 'search': search(key, value)
    elif verb == 'install' and value is not None: install(key, value)
    elif verb == 'states': states(key)
    elif verb == 'state-slot' and value is not None: states(key, delta=value)
    elif verb == 'state-recover' and value is not None: states(key, recovery=value)
    else: raise ValueError('Unknown ROM asset action')


if __name__ == '__main__':
    try:
        run(sys.argv[1:])
    except Exception as error:
        print(clean(error, 150), file=sys.stderr)
        sys.exit(1)

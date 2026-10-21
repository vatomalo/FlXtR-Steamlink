#!/usr/bin/env python3
"""Bounded, short-lived metadata/link worker. Never downloads a whole track."""
import hashlib
from itertools import islice
import html
import json
import os
from pathlib import Path
import re
import ssl
import sys
import time
import unicodedata
import urllib.request
from urllib.parse import urlsplit, parse_qs

CACHE = Path('music-cache')
MAX_ROWS = 100

def clean(value, limit=150):
    value = unicodedata.normalize('NFKD', str(value or '')).encode('ascii', 'ignore').decode()
    return re.sub(r'[\x00-\x1f\x7f]', ' ', value)[:limit]

def spotify_id(value):
    m = re.fullmatch(r'https://open\.spotify\.com/(?:intl-[a-z]+/)?playlist/([A-Za-z0-9]{22})(?:\?[^\s]*)?', value)
    return m.group(1) if m else None

def spotify_rows(document):
    m = re.search(r'<script[^>]+id="__NEXT_DATA__"[^>]*>(.*?)</script>', document, re.S)
    if not m:
        raise ValueError('Spotify did not expose public playlist metadata')
    data = json.loads(html.unescape(m.group(1)))
    entity = data['props']['pageProps']['state']['data']['entity']
    rows = []
    for track in entity.get('trackList', [])[:MAX_ROWS]:
        title, artist = clean(track.get('title')), clean(track.get('subtitle'))
        if title:
            rows.append(('query', clean(f'{artist} {title}', 220), title, artist))
    if not rows:
        raise ValueError('Playlist is private, empty or unavailable; use a public playlist URL')
    return rows

def fetch_spotify(value):
    ident = spotify_id(value)
    if not ident:
        raise ValueError('Use a public open.spotify.com/playlist/ URL')
    context = ssl.create_default_context(cafile=os.environ.get('SSL_CERT_FILE'))
    req = urllib.request.Request(f'https://open.spotify.com/embed/playlist/{ident}', headers={'User-Agent': 'Mozilla/5.0'})
    with urllib.request.urlopen(req, timeout=15, context=context) as response:
        data = response.read(2_000_001)
    if len(data) > 2_000_000:
        raise ValueError('Playlist response too large')
    return spotify_rows(data.decode('utf-8'))

def ydl_options(flat=False):
    options = {'quiet': True, 'no_warnings': True, 'socket_timeout': 15,
               'retries': 1, 'extractor_retries': 1, 'skip_download': True,
               'cachedir': str(CACHE / 'yt-dlp'), 'noplaylist': not flat,
               'extract_flat': 'in_playlist' if flat else False,
               'playlistend': 20, 'format': 'bestaudio[ext=m4a]/bestaudio',
               'js_runtimes': {'quickjs': {'path': str((Path(__file__).resolve().parent.parent / 'music-runtime/bin/qjs.exe') if os.name == 'nt' else Path('scripts/music-qjs.sh').resolve())}}}
    cert = os.environ.get('SSL_CERT_FILE')
    if cert:
        options['cacert'] = cert
    return options

def youtube_address(value):
    parsed = urlsplit(value)
    if parsed.scheme != 'https' or parsed.hostname not in ('youtube.com', 'www.youtube.com', 'music.youtube.com', 'youtu.be'):
        return None
    if parsed.username or parsed.password or parsed.netloc != parsed.hostname:
        return None
    query = parse_qs(parsed.query)
    playlist = query.get('list', [''])[0]
    if playlist and not re.fullmatch(r'[A-Za-z0-9_-]{1,128}', playlist):
        return None
    if parsed.path == '/playlist' and playlist:
        return 'https://www.youtube.com/playlist?list=' + playlist
    if parsed.hostname == 'youtu.be':
        ident = parsed.path[1:]
    elif parsed.path.startswith('/shorts/'):
        ident = parsed.path[len('/shorts/'):]
    elif parsed.path == '/watch':
        ident = query.get('v', [''])[0]
    else:
        return None
    if not re.fullmatch(r'[A-Za-z0-9_-]{11}', ident):
        return None
    return 'https://www.youtube.com/watch?v=' + ident + ('&list=' + playlist if playlist else '')

def search(value):
    if spotify_id(value):
        return fetch_spotify(value)
    if 'spotify.com' in value:
        raise ValueError('Use a public Spotify playlist URL')
    youtube_url = youtube_address(value)
    if value.startswith('https://') and not youtube_url:
        raise ValueError('Use a YouTube video/playlist URL or a public Spotify playlist')
    with youtube_client(True) as ydl:
        data = ydl.extract_info(youtube_url if youtube_url else 'ytsearch20:' + value[:220], download=False)
        if 'entries' in data:
            data['entries'] = list(islice(data['entries'], MAX_ROWS))
    if 'entries' not in data:
        data = {'entries': [data]}
    rows = []
    for entry in data.get('entries', []):
        if entry and re.fullmatch(r'[A-Za-z0-9_-]{11}', entry.get('id', '')):
            rows.append(('video', entry['id'], clean(entry.get('title')), clean(entry.get('channel') or entry.get('uploader'))))
    return rows

def recommendations():
    """Use YouTube's public radio mix, bounded and without replaying recent IDs."""
    try:
        history = json.loads((CACHE / 'history.txt').read_text())
    except (OSError, ValueError):
        history = []
    if not history or not re.fullmatch(r'[A-Za-z0-9_-]{11}', history[-1]):
        raise ValueError('No current YouTube song for recommendations')
    ident = history[-1]
    with youtube_client(True) as ydl:
        data = ydl.extract_info('https://www.youtube.com/watch?v=' + ident + '&list=RD' + ident, download=False)
        for entry in islice(data.get('entries', []), 20):
            if not entry:
                continue
            key = entry.get('id', '')
            if re.fullmatch(r'[A-Za-z0-9_-]{11}', key) and key not in history:
                resolve('video', key)
                return
    raise ValueError('No new YouTube recommendations available / START to search')

def resolve(kind, value):
    if kind == 'video' and re.fullmatch(r'[A-Za-z0-9_-]{11}', value):
        query = 'https://www.youtube.com/watch?v=' + value
    elif kind == 'query' and 0 < len(value) <= 220:
        query = 'ytsearch1:' + value
    else:
        raise ValueError('Invalid music selection')
    with youtube_client() as ydl:
        info = ydl.extract_info(query, download=False)
        if 'entries' in info:
            info = next((e for e in info['entries'] if e), None)
        if not info or not info.get('url', '').startswith('https://'):
            raise ValueError('No playable YouTube audio found')
        url = info['url']
        if len(url) > 16000 or '\n' in url or '\r' in url:
            raise ValueError('Invalid audio URL')
        headers = info.get('http_headers', {})
        request = url + '\n' + clean(headers.get('User-Agent', 'Mozilla/5.0'), 500) + '\n'
        atomic(CACHE / 'stream.txt', request)
        atomic(CACHE / 'playing.txt', clean(info.get('title')) + '\n')
        ident = info.get('id', '')
        if re.fullmatch(r'[A-Za-z0-9_-]{11}', ident):
            try:
                history = json.loads((CACHE / 'history.txt').read_text())
                if not isinstance(history, list):
                    history = []
            except (OSError, ValueError):
                history = []
            atomic(CACHE / 'history.txt', json.dumps((history + [ident])[-50:]))

def video_source(value, television=False, muxed=False, height=720):
    if height not in (480,720,1080):
        raise ValueError("Invalid video quality")
    history_path = CACHE / 'video-history.txt'
    try:
        history = json.loads(history_path.read_text())
        if not isinstance(history, list):
            history = []
    except (OSError, ValueError):
        history = []
    address = youtube_address(value)
    if '://' in value and not address:
        raise ValueError('Use a public YouTube video or playlist URL')
    if television:
        with youtube_client(True) as ydl:
            data = ydl.extract_info(address or 'ytsearch20:' + value[:220], download=False)
            entries = data.get('entries', [data])
            choices = [('video', e['id']) for e in islice(entries, 20)
                       if e and re.fullmatch(r'[A-Za-z0-9_-]{11}', e.get('id', ''))
                       and e['id'] not in history and not e.get('is_live')
                       and (e.get('duration') or 0) <= 1200]
        if not choices and history:
            with youtube_client(True) as ydl:
                data = ydl.extract_info('https://www.youtube.com/watch?v=' + history[-1] + '&list=RD' + history[-1], download=False)
                choices = [('video', e['id']) for e in islice(data.get('entries', []), 20)
                           if e and re.fullmatch(r'[A-Za-z0-9_-]{11}', e.get('id', '')) and e['id'] not in history]
        if not choices:
            raise ValueError('No new music videos available; change mtv-query.txt')
        address = 'https://www.youtube.com/watch?v=' + choices[0][1]
    options = ydl_options()
    combined = f'best[ext=mp4][vcodec^=avc1][height<={height}][acodec!=none]'
    options['format'] = combined if muxed else (
        f'bestvideo[ext=mp4][vcodec^=avc1][height<={height}]+bestaudio[ext=m4a]/' + combined)
    from yt_dlp import YoutubeDL
    from yt_dlp.extractor.youtube import YoutubeIE, YoutubeSearchIE, YoutubeTabIE
    with YoutubeDL(options, auto_init=False) as ydl:
        for extractor in (YoutubeIE, YoutubeSearchIE, YoutubeTabIE):
            ydl.add_info_extractor(extractor())
        info = ydl.extract_info(address or 'ytsearch1:' + value[:220], download=False)
        if 'entries' in info:
            info = next((e for e in info['entries'] if e), None)
        if not info or (television and (info.get('is_live') or (info.get('duration') or 0) > 1200)):
            raise ValueError('Music-video mode requires a clip under 20 minutes, not a live stream')
        formats = info.get('requested_formats') or [info]
        video = next((e for e in formats if e.get('vcodec') not in (None, 'none')), None)
        audio = next((e for e in formats if e.get('vcodec') == 'none' and e.get('acodec') != 'none'), None)
        if not video:
            raise ValueError('No compatible H.264 music video available')
        def checked(part, limit):
            value = part.get('url', '') if part else ''
            if part and (not value.startswith('https://') or len(value) > limit or any(c in value for c in '\r\n\t')):
                raise ValueError('Invalid video stream address')
            return value
        url, sound = checked(video, 2046), checked(audio, 16000)
        title = clean(info.get('title')) or 'YouTube video'
        agent = clean(info.get('http_headers', {}).get('User-Agent', 'Mozilla/5.0'), 500)
        ident = info.get('id', '')
        page = ('https://www.youtube.com/watch?v=' + ident
                if re.fullmatch(r'[A-Za-z0-9_-]{11}', ident) else '')
        atomic(CACHE / 'video.txt', '\n'.join([title, url, sound, agent, page]) + '\n')
        if television and re.fullmatch(r'[A-Za-z0-9_-]{11}', ident):
            atomic(history_path, json.dumps((history + [ident])[-50:]))

def youtube_client(flat=False):
    from yt_dlp import YoutubeDL
    from yt_dlp.extractor.youtube import YoutubeIE, YoutubeSearchIE, YoutubeTabIE
    client = YoutubeDL(ydl_options(flat), auto_init=False)
    for extractor in (YoutubeIE, YoutubeSearchIE, YoutubeTabIE):
        client.add_info_extractor(extractor())
    return client

def atomic(path, text):
    tmp = path.with_suffix(path.suffix + '.next')
    tmp.write_text(text, encoding='utf-8')
    os.replace(tmp, path)

def run(args):
    CACHE.mkdir(mode=0o700, exist_ok=True)
    if len(args) in (2,3) and args[0] in ('video', 'video-muxed', 'tv-video', 'tv-video-muxed'):
        video_source(args[1], television=args[0].startswith('tv-'), muxed=args[0].endswith('muxed'), height=int(args[2]) if len(args)==3 else 720); return
    if args == ['next']:
        recommendations(); return
    if args[0] == 'resolve' and len(args) == 3:
        resolve(args[1], args[2]); return
    if args[0] != 'search' or len(args) != 2:
        raise ValueError('Invalid music command')
    query = args[1].strip()[:220] or 'music'
    key = hashlib.sha256(query.encode()).hexdigest()
    cached = CACHE / (key + '.json')
    rows = None
    if cached.exists() and time.time() - cached.stat().st_mtime < 7200:
        try:
            rows = json.loads(cached.read_text())
        except (ValueError, OSError):
            pass
    if rows is None:
        rows = search(query)
        atomic(cached, json.dumps(rows))
        # Three recent result screens; no growing library daemon/database.
        for old in sorted(CACHE.glob('*.json'), key=lambda p: p.stat().st_mtime, reverse=True)[3:]:
            old.unlink(missing_ok=True)
    for row in rows[:MAX_ROWS]:
        if isinstance(row, (list, tuple)) and len(row) == 4:
            print('\t'.join(clean(cell, 220) for cell in row))

if __name__ == '__main__':
    try:
        run(sys.argv[1:])
    except Exception as error:
        CACHE.mkdir(mode=0o700, exist_ok=True)
        atomic(CACHE / 'error.txt', clean(str(error), 180))
        print('Music lookup failed: ' + clean(str(error), 180), file=sys.stderr)
        sys.exit(1)

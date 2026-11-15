#!/usr/bin/env python3
"""Match logged Secret of Mana song IDs to genre-specific YouTube covers.

Reads existing [som-ai] SONG 0xNN ... messages. Reuses FlXtR tools/music.py
(yt-dlp) and greenlink-audio. Never changes SPC channels or the SNES core.
"""
import argparse
import importlib.util
import json
import logging
import os
from pathlib import Path
import re
import signal
import subprocess
import sys
import time

SONG = re.compile(r"\[som-ai\]\s+SONG\s+0x([0-9a-fA-F]{1,2})\b")
GENRES = ("Lo-Fi", "Jazz", "City Pop", "Orchestral", "Piano",
          "Synthwave", "Rock", "Ambient", "Off")
SEARCH_HINTS = ("lofi chill remix", "jazz cover", "city pop remix",
                "orchestral arrangement", "piano cover", "synthwave remix",
                "rock cover", "ambient remix", "")
GENRE_WORDS = (("lofi", "lo-fi", "chillhop"), ("jazz",), ("city pop", "citypop"),
               ("orchestral", "orchestra"), ("piano",), ("synthwave", "retrowave"),
               ("rock", "metal"), ("ambient", "atmospheric"))
CUES = {0x17, 0x18, 0x19, 0x1A, 0x1E, 0x21, 0x22, 0x23,
        0x28, 0x2D, 0x2E, 0x2F, 0x30, 0x35}
ALIASES = {i: 0x38 for i in range(0x3B, 0x40)}
VIDEO = re.compile(r"[A-Za-z0-9_-]{11}\Z")
LOG = logging.getLogger("som-music")


def load_names(path):
    result = {}
    for line in path.read_text(encoding="utf-8-sig").splitlines():
        if not line or line.lstrip().startswith("#"):
            continue
        fields = line.split("\t", 1)
        if len(fields) != 2 or not fields[1].strip():
            raise ValueError("invalid song catalog row")
        index = int(fields[0], 16)
        if index not in range(64):
            raise ValueError("invalid song index")
        result[index] = fields[1].strip()
    return result


def parse_song(line):
    match = SONG.search(line)
    return int(match.group(1), 16) if match else None


def read_genre(path):
    try:
        raw = path.read_text().strip()
        value = int(raw) if raw.isdecimal() else GENRES.index(raw)
        return value if 0 <= value < len(GENRES) else 0
    except (OSError, ValueError):
        return 0


def search_query(title, genre):
    return ("Secret of Mana " + title.split(" / ")[0] + " " +
            SEARCH_HINTS[genre]).strip()[:220]


def candidate_score(row, title, genre):
    kind, ident, name, uploader = row
    if kind != "video" or not VIDEO.fullmatch(ident):
        return -1
    target = [w for w in re.findall(r"[a-z0-9]+", title.lower()) if len(w) > 2]
    description = (name + " " + uploader).lower()
    if not target or not all(w in description for w in target):
        return -1
    game_named = "mana" in description or "seiken" in description
    topic_release = uploader.lower().endswith(" - topic")
    if not game_named and not topic_release:
        return -1
    if not any(word in description for word in GENRE_WORDS[genre]):
        return -1
    return (10 if game_named else 5) + sum(word in name.lower() for word in target)


class MusicBridge:
    def __init__(self, names, genre_file, cache, player, dry_run=False):
        self.names, self.genre_file, self.cache = names, genre_file, cache
        self.player, self.dry_run = player, dry_run
        self.current, self.child, self.music = None, None, None
        try:
            stored = json.loads((cache / "selection.json").read_text())
            self.selections = stored if isinstance(stored, dict) else {}
        except (OSError, ValueError):
            self.selections = {}

    def get_music(self):
        if self.music is None:
            path = Path(__file__).with_name("music.py")
            spec = importlib.util.spec_from_file_location("flxtr_music", path)
            if spec is None or spec.loader is None:
                raise RuntimeError("FlXtR tools/music.py missing")
            module = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(module)
            module.CACHE = self.cache  # Never disturb normal music playback.
            self.music = module
        return self.music

    def stop(self):
        child, self.child = self.child, None
        if child and child.poll() is None:
            os.killpg(child.pid, signal.SIGTERM)
            try:
                child.wait(timeout=2)
            except subprocess.TimeoutExpired:
                os.killpg(child.pid, signal.SIGKILL)
                child.wait()

    def apply(self, index):
        if index in CUES:
            LOG.info("cue 0x%02X, keep current background track", index)
            return
        index = ALIASES.get(index, index)
        if index not in self.names:
            LOG.warning("unknown track 0x%02X", index)
            return
        genre = read_genre(self.genre_file)
        selected = index, genre
        if self.current == selected and (self.dry_run or (self.child and self.child.poll() is None)):
            return
        self.current = selected
        title = self.names[index]
        self.stop()
        if genre == len(GENRES) - 1:
            LOG.info("Original mode: stop external backing music")
            return
        query = search_query(title, genre)
        if self.dry_run:
            LOG.info("DRY RUN song=0x%02X title=%r genre=%s search=%r",
                     index, title, GENRES[genre], query)
            return
        cache_key = "%02X:%d" % selected
        try:
            music = self.get_music()
            ident = self.selections.get(cache_key)
            if not ident or not VIDEO.fullmatch(ident):
                results = music.search(query)
                ranked = sorted(((candidate_score(r, title, genre), r)
                                 for r in results if len(r) == 4),
                                key=lambda item: item[0], reverse=True)
                if not ranked or ranked[0][0] < 0:
                    LOG.warning("no matching %s version for %s", GENRES[genre], title)
                    return
                ident = ranked[0][1][1]
                self.selections[cache_key] = ident
                self.cache.mkdir(parents=True, exist_ok=True)
                tmp = self.cache / "selection.next"
                tmp.write_text(json.dumps(self.selections, indent=2) + "\n")
                tmp.replace(self.cache / "selection.json")
            music.resolve("video", ident)  # Refresh temporary signed audio URL.
            self.child = subprocess.Popen([self.player, str(self.cache / "stream.txt")],
                                          start_new_session=True)
            LOG.info("PLAY 0x%02X %s [%s] YouTube ID %s", index, title, GENRES[genre], ident)
        except (OSError, ValueError, RuntimeError, KeyError) as exc:
            LOG.warning("music lookup/playback failed for %s: %s", title, type(exc).__name__)

    def line(self, text):
        index = parse_song(text)
        if index is not None:
            self.apply(index)

    def poll_genre(self):
        if self.current is not None and read_genre(self.genre_file) != self.current[1]:
            index = self.current[0]
            self.current = None
            self.apply(index)


def pid_alive(pid):
    try:
        os.kill(pid, 0)
        return True
    except ProcessLookupError:
        return False
    except PermissionError:
        return True


def tail(path, bridge, from_start=False, alive=None, som_wait=None, clock=time.monotonic):
    """Follow `path`. Returns when alive() turns false (the game exited) or,
    with som_wait seconds, when no [som-ai] line appeared by then (another
    game: the launcher starts the bridge for every RetroArch session)."""
    started, seen_som = clock(), False
    with path.open(encoding="utf-8", errors="replace") as stream:
        if not from_start:
            stream.seek(0, 2)
        while True:
            line = stream.readline()
            if line:
                seen_som = seen_som or "[som-ai]" in line
                bridge.line(line)
                continue
            if alive is not None and not alive():
                LOG.info("game exited: stop")
                return "exited"
            if som_wait is not None and not seen_som and clock() - started > som_wait:
                LOG.info("not Secret of Mana through the AI core: stop")
                return "not-som"
            bridge.poll_genre()
            time.sleep(.25)
            if stream.tell() > path.stat().st_size:
                stream.seek(0)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--catalog", type=Path, default=Path("data/som_song_names.tsv"))
    parser.add_argument("--genre-file", type=Path,
                        default=Path("/home/apps/retroarch/cores/experimental/som_music_genre"))
    parser.add_argument("--cache", type=Path, default=Path("som-music-cache"))
    parser.add_argument("--player", default="./greenlink-audio")
    parser.add_argument("--log", type=Path, help="tail actual RetroArch log (else stdin)")
    parser.add_argument("--from-start", action="store_true")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--game-pid", type=int,
                        help="stop playback and exit when this process ends")
    parser.add_argument("--som-wait", type=float,
                        help="exit if no [som-ai] line within this many seconds")
    args = parser.parse_args(argv)
    logging.basicConfig(level=logging.INFO, format="[som-music] %(message)s")
    bridge = MusicBridge(load_names(args.catalog), args.genre_file, args.cache,
                         args.player, args.dry_run)
    def finish(*_):
        bridge.stop()
        raise SystemExit(0)
    signal.signal(signal.SIGINT, finish)
    signal.signal(signal.SIGTERM, finish)
    try:
        if args.log:
            alive = (lambda: pid_alive(args.game_pid)) if args.game_pid else None
            tail(args.log, bridge, args.from_start, alive, args.som_wait)
        else:
            for line in sys.stdin:
                bridge.poll_genre()
                bridge.line(line)
    finally:
        bridge.stop()


if __name__ == "__main__":
    main()

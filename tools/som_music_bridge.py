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
# A player that exits with an error this soon after starting could not open
# the audio output (on the Steam Link: MV_PE_StreamCreate() failed while
# RetroArch holds the HDMI stream). After this many in a row the bridge
# stops trying for the session instead of relaunching on every song.
QUICK_FAILURE_SECONDS = 10.0
QUICK_FAILURES_TO_GIVE_UP = 2
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


def candidate_score(row, title, genre, require_genre=True):
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
    if require_genre and not any(word in description for word in GENRE_WORDS[genre]):
        return -1
    return (10 if game_named else 5) + sum(word in name.lower() for word in target)


DEFAULT_MIX_RATE = 32040


def mix_rate(path):
    """The game's sample rate published by the proxy (Hz), else 32040."""
    try:
        rate = int(Path(path).read_text().strip())
        return rate if 8000 <= rate <= 96000 else DEFAULT_MIX_RATE
    except (OSError, ValueError, TypeError):
        return DEFAULT_MIX_RATE


PCM_CACHE_BUDGET = 1500 * 1024 * 1024   # decoded arrangements kept on disk
MIN_PCM_SECONDS = 5


class MusicBridge:
    def __init__(self, names, genre_file, cache, player, dry_run=False,
                 mix_control=None, rate_file=None):
        self.names, self.genre_file, self.cache = names, genre_file, cache
        self.player, self.dry_run = player, dry_run
        # Mixing mode: the proxy mixes a decoded file into the game's own
        # audio (one stream; the Steam Link refuses a second one). The single
        # CPU core is busy emulating, so each arrangement is decoded once,
        # at the lowest priority, into a cache file and only complete files
        # are played (looped by the proxy); the current track keeps playing
        # while the next one decodes.
        self.mix_control, self.rate_file = mix_control, rate_file
        self.pending = None   # (index, genre, ident, final path, part path)
        self.current, self.child, self.music = None, None, None
        self.started, self.quick_failures, self.gave_up = 0.0, 0, False
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
        pending, self.pending = getattr(self, "pending", None), None
        child, self.child = self.child, None
        if child and child.poll() is None:
            os.killpg(child.pid, signal.SIGTERM)
            try:
                child.wait(timeout=2)
            except subprocess.TimeoutExpired:
                os.killpg(child.pid, signal.SIGKILL)
                child.wait()
        if pending:
            pending[4].unlink(missing_ok=True)       # an unfinished decode

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
        if self.gave_up:
            return
        if self.current == selected and (self.dry_run or (self.child and self.child.poll() is None)):
            return
        self.current = selected
        title = self.names[index]
        if genre == len(GENRES) - 1:
            self.stop()
            self.pending = None
            if self.mix_control:
                self.set_control(None)
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
                ident = self.find(music, query, title, genre, True)
                if ident is None:
                    # No cover in this genre: any Secret of Mana arrangement
                    # of the piece is better than silence.
                    ident = self.find(music, search_query(title, len(GENRES) - 1),
                                      title, genre, False)
                    if ident is not None:
                        LOG.info("no %s version of %s: using another arrangement",
                                 GENRES[genre], title)
                if ident is None:
                    LOG.warning("no version of %s found; %s", title,
                                "keeping the current track" if self.child else "silence")
                    return
                self.selections[cache_key] = ident
                self.cache.mkdir(parents=True, exist_ok=True)
                tmp = self.cache / "selection.next"
                tmp.write_text(json.dumps(self.selections, indent=2) + "\n")
                tmp.replace(self.cache / "selection.json")
            if self.mix_control:
                self.mix(index, genre, title, ident, music)
                return
            music.resolve("video", ident)  # Refresh temporary signed audio URL.
            self.stop()                    # only now: a replacement is ready
            self.child = subprocess.Popen(self.player_command(), start_new_session=True)
            self.started = time.monotonic()
            LOG.info("PLAY 0x%02X %s [%s] YouTube ID %s", index, title, GENRES[genre], ident)
        except Exception as exc:  # yt-dlp's DownloadError (offline) is a plain Exception
            LOG.warning("music lookup/playback failed for %s: %s", title, type(exc).__name__)

    @staticmethod
    def find(music, query, title, genre, require_genre):
        results = music.search(query)
        ranked = sorted(((candidate_score(r, title, genre, require_genre), r)
                         for r in results if len(r) == 4),
                        key=lambda item: item[0], reverse=True)
        return ranked[0][1][1] if ranked and ranked[0][0] >= 0 else None

    def player_command(self):
        return [self.player, str(self.cache / "stream.txt")]

    # Mixing mode ---------------------------------------------------------
    def pcm_path(self, ident):
        return self.cache / "pcm" / ("%s-%d.raw" % (ident, mix_rate(self.rate_file)))

    def set_control(self, path):
        """Name the file the proxy mixes; None stops the mixed music."""
        if path is None:
            try:
                Path(self.mix_control).unlink()
            except FileNotFoundError:
                pass
            return
        tmp = Path(str(self.mix_control) + ".next")
        tmp.write_text(str(Path(path).resolve()) + "\n")
        tmp.replace(self.mix_control)

    def mix(self, index, genre, title, ident, music):
        final = self.pcm_path(ident)
        if final.is_file():
            self.stop()                                   # a decode for another track
            os.utime(final)                               # most recently used
            self.set_control(final)
            LOG.info("PLAY 0x%02X %s [%s] YouTube ID %s (cached)", index, title,
                     GENRES[genre], ident)
            return
        if self.pending and self.pending[2] == ident and self.child:
            return                                        # already decoding it
        music.resolve("video", ident)
        self.stop()
        final.parent.mkdir(parents=True, exist_ok=True)
        part = final.with_suffix(".part")
        self.pending = (index, genre, ident, final, part)
        self.child = subprocess.Popen(
            [self.player, "--pcm", str(mix_rate(self.rate_file)), str(part),
             str(self.cache / "stream.txt")],
            start_new_session=True, preexec_fn=lambda: os.nice(19))
        self.started = time.monotonic()
        LOG.info("DECODE 0x%02X %s [%s] YouTube ID %s at low priority; the current "
                 "track plays meanwhile", index, title, GENRES[genre], ident)

    def finish_decode(self, code):
        pending, self.pending = self.pending, None
        if not pending:
            return
        index, genre, ident, final, part = pending
        size = part.stat().st_size if part.exists() else 0
        if code != 0 or size < MIN_PCM_SECONDS * 4 * mix_rate(self.rate_file):
            part.unlink(missing_ok=True)
            LOG.warning("decode of %s failed (exit %d, %d bytes)", ident, code, size)
            return
        part.replace(final)
        self.prune_cache(keep=final)
        if self.current == (index, genre):
            self.set_control(final)
            LOG.info("PLAY 0x%02X %s [%s] YouTube ID %s (decoded, %d s)", index,
                     self.names.get(index, "?"), GENRES[genre], ident,
                     size // 4 // mix_rate(self.rate_file))

    def prune_cache(self, keep=None):
        files = sorted((p for p in (self.cache / "pcm").glob("*.raw") if p != keep),
                       key=lambda p: p.stat().st_mtime)
        total = sum(p.stat().st_size for p in files) + (keep.stat().st_size if keep else 0)
        while files and total > PCM_CACHE_BUDGET:
            oldest = files.pop(0)
            total -= oldest.stat().st_size
            oldest.unlink()

    def check_player(self, now=None):
        """Notice a player that died: an error right after start counts as a
        quick failure; enough of them in a row end external music for this
        session. Returns the player's exit code once, else None."""
        child = self.child
        if child is None or child.poll() is None:
            return None
        self.child = None
        now = time.monotonic() if now is None else now
        if child.returncode != 0 and now - self.started < QUICK_FAILURE_SECONDS:
            self.quick_failures += 1
            LOG.warning("player exited with %d after %.1f s (%d quick failure(s))",
                        child.returncode, now - self.started, self.quick_failures)
            if self.quick_failures >= QUICK_FAILURES_TO_GIVE_UP and not self.gave_up:
                self.gave_up = True
                LOG.warning("audio output unavailable: external music off for this session")
        else:
            self.quick_failures = 0
            LOG.info("player finished (exit %d)", child.returncode)
        if self.mix_control:
            self.finish_decode(child.returncode)
        return child.returncode

    def line(self, text):
        index = parse_song(text)
        if index is not None:
            self.apply(index)

    def poll_genre(self):
        ended = self.check_player()
        if (ended is not None and not self.mix_control and not self.gave_up and
                self.current is not None):
            index, self.current = self.current[0], None
            self.apply(index)          # retry once (a track that ended, or one quick failure)
            return
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
    # Mixing into the game's audio is the default: on the Steam Link a
    # separate player cannot open a second audio stream while RetroArch plays.
    parser.add_argument("--mix-control", type=Path, default=Path("/tmp/flxtr-som-music.play"),
                        help="file naming the decoded track the AI proxy mixes into the game")
    parser.add_argument("--mix-fifo", type=Path, help=argparse.SUPPRESS)   # older launchers
    parser.add_argument("--rate-file", type=Path, default=Path("/tmp/flxtr-som-audio-rate"),
                        help="game sample rate written by the proxy")
    parser.add_argument("--no-mix", action="store_true",
                        help="play through a separate audio output instead")
    parser.add_argument("--game-pid", type=int,
                        help="stop playback and exit when this process ends")
    parser.add_argument("--som-wait", type=float,
                        help="exit if no [som-ai] line within this many seconds")
    args = parser.parse_args(argv)
    logging.basicConfig(level=logging.INFO, format="[som-music] %(message)s")
    bridge = MusicBridge(load_names(args.catalog), args.genre_file, args.cache,
                         args.player, args.dry_run,
                         None if args.no_mix else args.mix_control, args.rate_file)
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
        if bridge.mix_control:
            bridge.set_control(None)       # the game is gone: no music on the next one


if __name__ == "__main__":
    main()

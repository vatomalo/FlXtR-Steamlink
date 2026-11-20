#!/usr/bin/env python3
"""Record a lesson: the player shows something (cannon travel, 2026-11-20)
and every work-RAM snapshot plus every button press is kept for learning it.

Read-only. The proxy exports work RAM twice a second while the AI is not
playing, and logs the player's presses while /tmp/flxtr-som-ai-record
exists on the Steam Link. This tool saves each new snapshot (gzip) and a
line of the values that tell a lesson's steps apart: map, game mode, the
event VM state and script pointer ($D0-$D3, som-decomp cutscene-engine.md),
the transition byte $FF, the hero's tile, gold and every story flag
($7E:CF00) that changed. At the end the press log is fetched.

Stop the AI bridge first so nothing plays meanwhile; stop the recording by
creating the stop file (or Ctrl+C, or --seconds):

    systemctl --user stop flxtr-som-ai
    python3 tools/som_teach_record.py --name cannon
    touch /tmp/flxtr-teach-stop          # when the lesson is over
    systemctl --user start flxtr-som-ai

Output: ~/.local/share/flxtr/teach/<name>-<time>/ (frames/, steps.log,
presses.txt). Copied game memory never goes into the repository.
"""
import argparse
import gzip
import hashlib
import subprocess
import time
from pathlib import Path

from som_memory import WRAM

DEFAULT_OUT = Path.home() / ".local/share/flxtr/teach"
STOP_FILE = Path("/tmp/flxtr-teach-stop")
RECORD_MARKER = "/tmp/flxtr-som-ai-record"
RECORDED = "/tmp/flxtr-som-ai-recorded"
POLL_S = 0.4


def summary(wram):
    """The values that tell a lesson's steps apart."""
    def u8(a):
        return wram.u8(0x7E0000 + a)
    hero_x, hero_y = wram.u16(0x7EE002), wram.u16(0x7EE004)
    return {"map": wram.u16(0x7E00DC) & 0x1FF, "mode": (u8(0xE4), u8(0xE6)),
            "vm": u8(0xD0), "script": u8(0xD1) | u8(0xD2) << 8 | u8(0xD3) << 16,
            "ff": u8(0xFF), "tile": (hero_x // 16, hero_y // 16),
            "gold": u8(0xCC6A) | u8(0xCC6B) << 8 | u8(0xCC6C) << 16}


def flags(wram):
    return bytes(wram.u8(0x7ECF00 + i) for i in range(256))


def describe(before, after, flags_before, flags_after):
    """One steps.log line: the summary, and what changed."""
    changed = [f"{i:02x}:{a}->{b}" for i, (a, b) in enumerate(zip(flags_before, flags_after))
               if a != b] if flags_before is not None else []
    moved = [k for k in ("map", "mode", "vm", "script", "ff", "gold")
             if before is None or before[k] != after[k]]
    return (f"map {after['map']} mode {after['mode'][0]}/{after['mode'][1]:02x} "
            f"vm {after['vm']:02x} script {after['script']:06x} ff {after['ff']:02x} "
            f"tile {after['tile'][0]},{after['tile'][1]} gold {after['gold']}"
            + (f" | changed {','.join(moved)}" if moved else "")
            + (f" | flags {' '.join(changed)}" if changed else ""))


def remote(host, command, binary=False):
    from som_qwen_bridge import SSH_BASE
    result = subprocess.run([*SSH_BASE, host, command], capture_output=True, timeout=30)
    return result.stdout if binary else result.stdout.decode(errors="replace")


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--host", default="root@100.124.74.13")
    parser.add_argument("--name", default="lesson")
    parser.add_argument("--seconds", type=float, default=3600.0)
    parser.add_argument("--out", type=Path, default=DEFAULT_OUT)
    args = parser.parse_args()
    folder = args.out / f"{args.name}-{time.strftime('%Y%m%d-%H%M%S')}"
    (folder / "frames").mkdir(parents=True, exist_ok=True)
    STOP_FILE.unlink(missing_ok=True)
    remote(args.host, f"rm -f {RECORDED}; touch {RECORD_MARKER}")
    print(f"recording into {folder}; touch {STOP_FILE} to stop", flush=True)
    deadline = time.monotonic() + args.seconds
    last_hash, before, flags_before, count, start = None, None, None, 0, time.monotonic()
    log = open(folder / "steps.log", "w")
    try:
        while time.monotonic() < deadline and not STOP_FILE.exists():
            raw = remote(args.host, "cat /tmp/flxtr-som-wram.bin", binary=True)
            if len(raw) == 0x20000:
                digest = hashlib.sha1(raw).hexdigest()
                if digest != last_hash:
                    last_hash = digest
                    wram = WRAM(raw)
                    after, flags_after = summary(wram), flags(wram)
                    stamp = time.monotonic() - start
                    with gzip.open(folder / "frames" / f"{count:05d}.bin.gz", "wb") as out:
                        out.write(raw)
                    log.write(f"{count:05d} {stamp:7.1f}s {describe(before, after, flags_before, flags_after)}\n")
                    log.flush()
                    before, flags_before, count = after, flags_after, count + 1
            time.sleep(POLL_S)
    except KeyboardInterrupt:
        pass
    finally:
        log.close()
        remote(args.host, f"rm -f {RECORD_MARKER}")
        (folder / "presses.txt").write_text(remote(args.host, f"cat {RECORDED} 2>/dev/null"))
    print(f"{count} snapshots -> {folder}", flush=True)


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Steam Link side of the SoM AI observation link, one process per SSH session.

Drop-in replacement for som_qwen_bridge.REMOTE_LOOP (same requests, same
replies) plus one cheaper request, `dlt`/`dltm`. Measured on the Steam Link
(single Cortex-A9 core shared with the game, 2026-11-15): the shell loop forked
`[`, `printf` and `gzip` for every decision, and the gzip'd hot ranges (~7.9
KB per read) then crossed SSH and Tailscale's userspace WireGuard, about 20 %
of the CPU together. Here nothing is forked per request, and `dlt` sends the
whole 128 KiB snapshot as the 512-byte blocks that changed since the image
sent last time in this session: `WRAMB <n>` + n bytes of zlib(32-byte block
bitmap + those blocks). The bridge always gets a complete, current image
(never a hot patch on a stale base). Measured per request on the device:
shell loop `hotmz` 16 ms CPU and ~7.4 KB; `dlt` about 4 ms and ~1-2 KB.

Both ends start from an all-zero base when the session starts, so the first
reply carries every non-zero block. A failed or partial read makes the bridge
drop the session, which also drops this process and its base.

Only fixed paths are read or written; requests are matched exactly and
action/message text is validated like the shell loop did. Standard library
only (the device runs it with the bundled musl Python, scripts/music-runtime.sh).
"""
import argparse
import os
import re
import sys
import zlib

BANNER = b"SOMWS 1\n"
WRAM_BYTES = 0x20000
HOT_RANGES_KB = ((0, 8), (39, 2), (50, 10), (96, 4))   # som_qwen_bridge.HOT_RANGES_KB
BLOCK = 512
BLOCKS = WRAM_BYTES // BLOCK
OUTER = 4096
GAMBIT_DEFAULTS = "433341"
COMBAT_DEFAULTS = "32223333"
ACTION_TEXT = re.compile(r"[a-z0-9_]+\Z")
MESSAGE_TEXT = re.compile(r"[A-Za-z0-9 .,!?:/%+=()|^-]+\Z")
# --profile living: what tools/som_living_world.REMOTE_LOOP allowed, no more.
LIVING_COMMANDS = {"obs", "obsz", "dlt"}
LIVING_MESSAGE = re.compile(r"[A-Za-z0-9 .,!?:/%+=()|-]+\Z")
NPC_WALK = re.compile(r"npc_walk [0-9 ]+\Z")


def block_delta(base, image):
    """32-byte bitmap of changed BLOCK-sized blocks, then those blocks.

    Two levels (4 KiB, then 512 B): on the A9 this is 1.9 ms per snapshot
    against 5.6 ms comparing all 256 blocks and 10 ms for a big-int XOR."""
    bitmap, blocks = bytearray(BLOCKS // 8), []
    for outer in range(0, WRAM_BYTES, OUTER):
        if base[outer:outer + OUTER] == image[outer:outer + OUTER]:
            continue
        for lo in range(outer, outer + OUTER, BLOCK):
            chunk = image[lo:lo + BLOCK]
            if base[lo:lo + BLOCK] != chunk:
                i = lo // BLOCK
                bitmap[i >> 3] |= 1 << (i & 7)
                blocks.append(chunk)
    return bytes(bitmap) + b"".join(blocks)


def apply_block_delta(base, delta):
    """Inverse of block_delta; returns the new image (bytes)."""
    image = bytearray(base)
    bitmap, offset = delta[:BLOCKS // 8], BLOCKS // 8
    for i in range(BLOCKS):
        if bitmap[i >> 3] >> (i & 7) & 1:
            if offset + BLOCK > len(delta):
                raise ValueError("truncated WRAM delta")
            image[i * BLOCK:(i + 1) * BLOCK] = delta[offset:offset + BLOCK]
            offset += BLOCK
    if offset != len(delta):
        raise ValueError("WRAM delta has trailing bytes")
    return bytes(image)


class Server:
    def __init__(self, tmp="/tmp", config="/home/apps/retroarch/cores/experimental",
                 profile="ai"):
        self.living = profile == "living"
        self.wram = os.path.join(tmp, "flxtr-som-wram.bin")
        self.hot = os.path.join(tmp, "flxtr-som-hot.bin")
        self.marker = os.path.join(tmp, "flxtr-som-ai-enabled")
        self.action = os.path.join(tmp, "flxtr-som-ai-action")
        self.message = os.path.join(tmp, "flxtr-som-ai-message")
        self.npc = os.path.join(tmp, "flxtr-som-npc-command")
        self.familiar = os.path.join(tmp, "flxtr-som-familiar")
        self.gambits = os.path.join(config, "som_ai_gambits")
        self.combat = os.path.join(config, "som_ai_combat")
        self.disciplines = os.path.join(config, "som_ai_disciplines")
        self.base = bytes(WRAM_BYTES)       # last image sent by `dlt`

    @staticmethod
    def _read(path):
        try:
            with open(path, "rb") as handle:
                return handle.read()
        except OSError:
            return None

    def _snapshot(self):
        data = self._read(self.wram)
        return data if data is not None and len(data) == WRAM_BYTES else None

    @staticmethod
    def _gzip(data):
        packer = zlib.compressobj(1, zlib.DEFLATED, 31)
        return packer.compress(data) + packer.flush()

    def _first_line(self, path):
        data = self._read(path)
        return data.split(b"\n", 1)[0].decode("ascii", "replace").strip() if data else ""

    def _write_line(self, path, text):
        try:
            with open(path, "w") as handle:
                handle.write(text + "\n")
            return b"OK\n"
        except OSError:
            return b"BAD\n"

    def handle(self, cmd):
        """Reply bytes for one request line (without its newline)."""
        if self.living:
            if cmd.startswith("npc "):
                text = cmd[4:]
                return self._write_line(self.npc, text) if NPC_WALK.match(text) else b"BAD\n"
            if cmd.startswith("msg "):
                text = cmd[4:]
                return (self._write_line(self.message, text) if LIVING_MESSAGE.match(text)
                        else b"BAD\n")
            if cmd not in LIVING_COMMANDS:
                return b"BAD\n"
        marked = cmd in ("obsm", "hotmz", "obsmz", "dltm", "nav")
        if marked and not os.path.exists(self.marker):
            return b"STOP\n"
        if cmd in ("dlt", "dltm"):
            image = self._snapshot()
            if image is None:
                return b"NONE\n"
            body = zlib.compress(block_delta(self.base, image), 1)
            self.base = image               # the bridge holds this image once it reads body
            return b"WRAMB %d\n" % len(body) + body
        if cmd in ("obs", "obsm"):
            image = self._snapshot()
            return b"WRAM\n" + image if image is not None else b"NONE\n"
        if cmd in ("obsz", "obsmz"):
            image = self._snapshot()
            return b"WRAMZ\n" + self._gzip(image) if image is not None else b"NONE\n"
        if cmd in ("hotz", "hotmz"):
            hot = self._read(self.hot)
            if hot is None or len(hot) != sum(c for _, c in HOT_RANGES_KB) * 1024:
                image = self._snapshot()
                if image is None:
                    return b"NONE\n"
                hot = b"".join(image[s * 1024:(s + c) * 1024] for s, c in HOT_RANGES_KB)
            return b"WRAMH\n" + self._gzip(hot)
        if cmd == "nav":
            image = self._snapshot()
            return (b"NAV\n" + image[:256] + image[224 * 256:226 * 256]
                    if image is not None else b"NONE\n")
        if cmd == "gmc":
            m = self._first_line(self.combat)
            ok = len(m) == len(COMBAT_DEFAULTS) and all(c in "012345" for c in m)
            return ("GMC %s\n" % (m if ok else COMBAT_DEFAULTS)).encode()
        if cmd == "gmb":
            m = self._first_line(self.gambits)
            ok = len(m) == len(GAMBIT_DEFAULTS) and all(c in "012345" for c in m)
            return ("GMB %s\n" % (m if ok else GAMBIT_DEFAULTS)).encode()
        if cmd == "dsc":            # the selected discipline ("v1 <n> ...")
            words = self._first_line(self.disciplines).split()
            n = words[1] if len(words) > 1 and words[0] == "v1" and words[1].isdigit() else "255"
            return ("DSC %s\n" % n).encode()
        if cmd == "fam":
            f = self._first_line(self.familiar)
            return ("FAM %s\n" % (f if f in ("1", "2", "3") else "0")).encode()
        if cmd.startswith("act "):
            text = cmd[4:]
            return self._write_line(self.action, text) if ACTION_TEXT.match(text) else b"BAD\n"
        if cmd.startswith("msg "):
            text = cmd[4:]
            return self._write_line(self.message, text) if MESSAGE_TEXT.match(text) else b"BAD\n"
        return b"BAD\n"


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--tmp", default="/tmp")
    parser.add_argument("--config", default="/home/apps/retroarch/cores/experimental")
    parser.add_argument("--profile", choices=("ai", "living"), default="ai")
    args = parser.parse_args(argv)
    server = Server(args.tmp, args.config, args.profile)
    out = sys.stdout.buffer
    out.write(BANNER)
    out.flush()
    for raw in sys.stdin.buffer:
        out.write(server.handle(raw.rstrip(b"\r\n").decode("ascii", "replace")))
        out.flush()


if __name__ == "__main__":
    try:
        main()
    except (BrokenPipeError, KeyboardInterrupt):
        pass

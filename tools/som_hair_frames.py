#!/usr/bin/env python3
"""Hair styles as prepared frames (player, 2026-11-28: runtime hair guessing
"its really bad"; chose prepared frames, Bald on the boy first).

Every frame the hero can show is read from the player's ROM (som_hero_gfx:
every weapon type's frame table), the style is made on the whole frame here,
offline, and the frame is cut back into its 16 x 16 pieces. Each piece that
changed is stored under the fingerprint of the original piece's tile bytes
(the game copies those bytes from the ROM into video memory unchanged), so at
run time the proxy recognises exactly which piece the game draws and hands
the core the prepared one. A piece with no entry stays the game's own.

    python3 tools/som_hair_frames.py ROM OUT.bin                 # the pack
    python3 tools/som_hair_frames.py ROM OUT.png --sheet         # every frame, before | after

Pack "SOMHAIRF1": u32 count, then count entries sorted by key: u64 key
(FNV-1a 64 of the piece's 4 x 32 tile bytes, top-left, top-right,
bottom-left, bottom-right; an empty tile is 32 zero bytes), 256 colour
indices (16 x 16, the piece as stored, before the game flips it).
"""
import argparse
import struct
import sys
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import som_hero_gfx as H  # noqa: E402

HAIR = {2, 3, 4}
SKIN = {5, 6, 7}
EYE = 15
SIZE, ORIGIN = 96, (48, 64)


def fnv64(data):
    h = 1469598103934665603
    for b in data:
        h ^= b
        h = (h * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return h or 1


def frames_of(gfx, hero):
    """{piece layout: (weapon, frame)} of every distinct frame."""
    seen = {}
    for weapon in [None] + list(H.WEAPON_TYPES):
        for frame in range(256):
            try:
                pieces = gfx.pieces(hero, weapon, frame)
            except Exception:
                continue
            if pieces:
                key = tuple((x, y, hf, vf, tuple(t)) for x, y, hf, vf, t in pieces)
                seen.setdefault(key, (weapon, frame))
    return seen


# Hand-made skulls (pixel art, not a computed shape: the ellipse came out as
# cones and boxes): o outline, H highlight, L light, M mid, D dark skin.
SKULL_FRONT = ["....ooooo....",
               "..ooLLLLLoo..",
               ".oLHHLLLLLMo.",
               ".oLHLLLLLLMo.",
               "oLLLLLLLLLLMo",
               "oLLLLLLLLLMMo",
               "oMLLLLLLLLMMo",
               "oMMLLLLLLMMDo"]
SKULL_SIDE = [".....ooooo....",                # facing right: the back of the head on the left
              "...ooLLLLLoo..",
              "..oLLHHLLLLMo.",
              ".oLLHLLLLLLLMo",
              "oLLLLLLLLLLLMo",
              "oMLLLLLLLLLLMo",
              "oMLLLLLLLLLMMo",
              ".oDMLLLLLLMMo."]
SKULL_BACK = ["....ooooo....",
              "..ooLLLLLoo..",
              ".oLHHLLLLLMo.",
              ".oLHLLLLLLMo.",
              "oLLLLLLLLLLMo",
              "oLLLLLLLLLLMo",
              "oMLLLLLLLLLMo",
              "oMLLLLLLLLMMo",
              "oMMLLLLLLMMDo",
              ".oDMMLLLMMDo.",
              "..oDMMMMMDo..",
              "...ooooooo..."]
SKULL_INK = {"o": 1, "H": 7, "L": 7, "M": 6, "D": 5}


def bald(image, shave=1):
    """The frame without hair, from its own head (player, 2026-11-28: the
    template skulls "do not fit at all" in many poses): the game's hair is
    shaved from the outside by `shave` pixels (only where it meets open
    background, never where it touches the face or body), spike tips that
    are left are smoothed away, and what remains is the skull: skin, lit
    from the top left, with a fresh outline. So every pose, tilt and turn
    keeps its own head shape. The face, eyes and headband are never touched."""
    h, w = len(image), len(image[0])
    out = [row[:] for row in image]
    hair = {(x, y) for y in range(h) for x in range(w) if image[y][x] in HAIR}
    if not hair:
        return out
    near = lambda x, y: [(x + dx, y + dy) for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1))]
    ring = lambda x, y: [(x + dx, y + dy) for dx in (-1, 0, 1) for dy in (-1, 0, 1) if dx or dy]
    # the hair as one shape: its dark strands (outline colour mostly among
    # hair), its rim (dark pixels touching hair and open background) and any
    # pocket it closes in
    strands = {(x, y) for y in range(h) for x in range(w) if image[y][x] == 1 and
               sum(p in hair for p in ring(x, y)) >= 3}
    rim = {(x, y) for y in range(h) for x in range(w) if image[y][x] == 1 and
           any(p in hair for p in near(x, y)) and
           any(0 <= px < w and 0 <= py < h and image[py][px] == 0 for px, py in near(x, y))}
    head = hair | strands | rim
    outside, todo = set(), [(0, 0)]
    while todo:                                  # the background reachable from the corner
        x, y = todo.pop()
        if (x, y) in outside or not (0 <= x < w and 0 <= y < h) or (x, y) in head or image[y][x]:
            continue
        outside.add((x, y))
        todo += near(x, y)
    xs = [x for x, _ in hair]; ys = [y for _, y in hair]
    for y in range(min(ys), max(ys) + 1):
        for x in range(min(xs), max(xs) + 1):
            if (x, y) not in head and (x, y) not in outside and image[y][x] in (0, 1) and \
                    sum(p in head for p in near(x, y)) >= 3:
                head.add((x, y))
    open_bg = lambda x, y: (x, y) in outside or not (0 <= x < w and 0 <= y < h)
    keep = set(head)
    for _ in range(shave + 1):                   # the rim, then `shave` layers of hair
        edge = {p for p in keep if any(q not in keep and (open_bg(*q) or q in head) for q in near(*p))}
        keep -= edge
    for _ in range(2):                           # spike tips: fewer than two kept neighbours
        keep -= {p for p in keep if sum(q in keep or (q not in head and not open_bg(*q)) for q in near(*p)) < 2}
    for x, y in head:
        out[y][x] = 0
    for x, y in keep:
        boundary = any(q not in keep and (open_bg(*q) or q in head) for q in near(x, y))
        if boundary:
            out[y][x] = 1
        else:
            lit = (x - 1, y) not in keep or (x, y - 1) not in keep or (x - 1, y - 1) not in keep
            dark = (x + 1, y) not in keep or (x + 2, y) not in keep or (x, y + 1) not in keep
            out[y][x] = 7 if lit and not dark else 5 if dark and not lit else 6 if dark else 7
    return out


def tile_bytes(gfx, offset):
    return bytes(32) if offset is None else gfx.rom[offset:offset + 32]


def cut(gfx, pieces, image):
    """{key: 16 x 16 indices as stored} of each piece cut from the image."""
    out = {}
    for x, y, hf, vf, tiles in pieces:
        key = fnv64(b"".join(tile_bytes(gfx, t) for t in tiles))
        px, py = ORIGIN[0] + x, ORIGIN[1] + y
        stored = []
        for v in range(16):
            for u in range(16):
                sx, sy = (15 - u if hf else u), (15 - v if vf else v)
                stored.append(image[py + sy][px + sx])
        out[key] = (bytes(stored), tiles)
    return out


def build(gfx, hero="boy"):
    entries, conflicts, frames = {}, 0, []
    for layout, (weapon, frame) in frames_of(gfx, hero).items():
        pieces = [(x, y, hf, vf, list(t)) for x, y, hf, vf, t in layout]
        if any(not (0 <= ORIGIN[0] + x and ORIGIN[0] + x + 16 <= SIZE and 0 <= ORIGIN[1] + y and
                    ORIGIN[1] + y + 16 <= SIZE) for x, y, _, _, _ in pieces):
            continue                       # not a real frame (past the end of a table)
        native = gfx.render(hero, pieces, size=SIZE, origin=ORIGIN)
        styled = bald(native)
        frames.append((native, styled))
        before, after = cut(gfx, pieces, native), cut(gfx, pieces, styled)
        for key, (data, _) in after.items():
            if data == before[key][0]:
                continue
            if key in entries and entries[key] != data:
                conflicts += 1
                continue
            entries[key] = data
    return entries, conflicts, frames


def pack(entries):
    out = bytearray(b"SOMHAIRF1") + struct.pack("<I", len(entries))
    for key in sorted(entries):
        out += struct.pack("<Q", key) + entries[key]
    return bytes(out)


def sheet(gfx, frames, path, hero="boy", scale=3):
    pal = [tuple(c) for c in gfx.palette(hero)]
    cols = 12
    cw, ch = 2 * 40, 48
    rows = (len(frames) + cols - 1) // cols
    w, h = cols * cw * scale, rows * ch * scale
    pix = [[(30, 40, 44)] * w for _ in range(h)]
    for n, (a, b) in enumerate(frames):
        gx, gy = (n % cols) * cw, (n // cols) * ch
        for k, img in enumerate((a, b)):
            for y in range(48):
                for x in range(40):
                    v = img[ORIGIN[1] - 40 + y][ORIGIN[0] - 20 + x]
                    if v:
                        for dy in range(scale):
                            yy = (gy + y) * scale + dy
                            xx = (gx + k * 40 + x) * scale
                            pix[yy][xx:xx + scale] = [pal[v]] * scale
    raw = b"".join(b"\0" + bytes(c for p in r for c in p) for r in pix)
    def chunk(t, c):
        return struct.pack(">I", len(c)) + t + c + struct.pack(">I", zlib.crc32(t + c) & 0xFFFFFFFF)
    Path(path).write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
                           + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("rom")
    ap.add_argument("out")
    ap.add_argument("--sheet", action="store_true")
    a = ap.parse_args()
    gfx = H.HeroGfx(Path(a.rom).read_bytes())
    entries, conflicts, frames = build(gfx)
    if a.sheet:
        sheet(gfx, frames, a.out)
    else:
        Path(a.out).write_bytes(pack(entries))
    print("%d frames, %d pieces changed, %d conflicting pieces kept first" % (len(frames), len(entries), conflicts),
          file=sys.stderr)


if __name__ == "__main__":
    main()

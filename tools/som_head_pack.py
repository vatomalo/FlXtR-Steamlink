#!/usr/bin/env python3
"""The game's own heads as hair styles for the character creator.

Player, 2026-11-27: drawn hair caps were "the worst! can't we use ingame
hairstyles?" ... "all humans". Every person in the ROM (the three heroes and
the villagers, read by som_hero_gfx) gives a head: the top row of its
standing frame, facing up, down and side, cropped to 16 x 16 on the head.
Monsters and objects are left out (no skin, or no face).

Colours are stored by role so a head can be worn in the hero's own palette:
1 outline, 2-4 hair (dark to light), 5-7 skin (dark to light), 15 white,
16+ other colours (RGB in the entry, matched to the nearest hero colour at
run time). The source's own hair ramp is kept for the NATIVE hair colour.

    python3 tools/som_head_pack.py ROM OUT.bin            # the proxy pack (SOMHEAD1)
    python3 tools/som_head_pack.py ROM OUT.png --sheet    # what was picked

Pack: "SOMHEAD3", u16 count, then per head: u8 kind, u8 hair[3][3] RGB,
u8 extras, u8 extra[8][3] RGB, u8 eyes[3][2] (the eye centre x, y per view,
255 none: from behind), then 3 x 16 x 16 role bytes (up, down, side; side
faces right; bit 0x80: part of the hairstyle, see hair_mask). The eyes place the hair on the hero (player, 2026-11-27: "use
the eyes to place the hairstyles").
"""
import argparse
import colorsys
import struct
import sys
import zlib
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import som_hero_gfx as H  # noqa: E402
import som_villager_work as V  # noqa: E402

SIZE = 16
DIRS = ("up", "down", "side")
SOURCES = ["boy", "girl", "sprite"] + list(range(0x83, 0x100))
KIND_IDS = {"boy": 0x80, "girl": 0x81, "sprite": 0x82}
# Passed the person test but are not people, or not a whole head, or the
# heroes' own heads again (sheet checked by eye, 2026-11-27): copies of the
# boy and girl, a crab, a cat, a reindeer, a turtle, a flame, a cone, a
# broken half, scenery, a rock, frog masks and the small full-body figures.
EXCLUDE = {0x8A, 0x8B, 0xA3, 0xA5, 0xAD, 0xAF, 0xB3, 0xC1, 0xCC, 0xD0, 0xD2, 0xDF,
           0xE1, 0xE2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF8}


def lum(c):
    return (c[0] * 30 + c[1] * 59 + c[2] * 11) / 100


def hsv(c):
    return colorsys.rgb_to_hsv(c[0] / 255, c[1] / 255, c[2] / 255)


def is_skin(c):
    h, s, v = hsv(c)
    return 0.02 <= h <= 0.13 and 0.15 <= s <= 0.62 and v >= 0.55 and c[0] >= 150


def head_image(gfx, kind, direction):
    """16 x 16 colour indices of the head (the frame's top row of pieces)."""
    pieces = V.standing(gfx, kind, direction)
    top = min(p[1] for p in pieces)
    row = [p for p in pieces if p[1] == top]
    image = gfx.render(kind, row, size=V.CELL, origin=V.ORIGIN)
    y0 = V.ORIGIN[1] + top
    cols = [x for y in range(y0, y0 + SIZE) for x, v in enumerate(image[y]) if v]
    if not cols:
        return None
    centre = (min(cols) + max(cols) + 1) // 2
    x0 = centre - SIZE // 2
    return [[image[y][x] if 0 <= x < len(image[0]) else 0 for x in range(x0, x0 + SIZE)]
            for y in range(y0, y0 + SIZE)]


def roles(palette, images):
    """{source index: role} and the hair ramp and extra colours."""
    count = Counter(v for im in images for row in im for v in row if v)
    used = [i for i in count if count[i] >= 1]
    role, extras = {}, []
    dark = [i for i in used if lum(palette[i]) < 80]
    for i in dark:
        role[i] = 1
    skin = sorted((i for i in used if i not in role and is_skin(palette[i])), key=lambda i: lum(palette[i]))
    for k, i in enumerate(skin):
        role[i] = 5 + min(2, round(k * 2 / max(1, len(skin) - 1))) if len(skin) > 1 else 7
    rest = [i for i in used if i not in role]
    whites = [i for i in rest if min(palette[i]) >= 215]
    for i in whites:
        if count[i] <= 12:
            role[i] = 15
    rest = [i for i in rest if i not in role]
    hair = []
    if rest:
        lead = max(rest, key=lambda i: count[i])
        lh, ls, _ = hsv(palette[lead])
        for i in rest:
            h, s, _ = hsv(palette[i])
            grey = ls < 0.18 and s < 0.25
            if i == lead or grey or (ls >= 0.18 and min(abs(h - lh), 1 - abs(h - lh)) < 0.09):
                hair.append(i)
    hair.sort(key=lambda i: lum(palette[i]))
    for k, i in enumerate(hair):
        role[i] = 2 + (min(2, round(k * 2 / (len(hair) - 1))) if len(hair) > 1 else 1)
    for i in used:
        if i not in role:
            if len(extras) < 8:
                extras.append(i)
                role[i] = 16 + len(extras) - 1
            else:
                role[i] = 1                 # a ninth extra colour: drawn dark
    ramp = []
    for want in (2, 3, 4):
        pick = [i for i in hair if role[i] == want]
        if pick:
            ramp.append(tuple(palette[pick[len(pick) // 2]]))
    while ramp and len(ramp) < 3:
        ramp.append(ramp[-1])
    return role, ramp, [tuple(palette[i]) for i in extras], skin, hair


def eye_centre(coded):
    """The eyes: non-skin pixels lying between skin in a row (whites, irises or
    pupils), in the first two rows where they occur. (x, y) or None."""
    skin = lambda v: v in (5, 6, 7)
    whites = [(x, y) for y, row in enumerate(coded) for x, v in enumerate(row) if v == 15]
    if len(whites) >= 2:                   # eye whites: the plain case
        return (sum(w[0] for w in whites) // len(whites), sum(w[1] for w in whites) // len(whites))
    for y, row in enumerate(coded):
        if y < 8:                          # the face is the lower half (light hair can look like skin)
            continue
        hits = []
        for x, v in enumerate(row):
            if not v or skin(v):
                continue
            left = any(skin(row[k]) for k in range(max(0, x - 3), x))
            right = any(skin(row[k]) for k in range(x + 1, min(len(row), x + 4)))
            if left and right:
                hits.append((x, y))
        if hits and y + 1 < len(coded):
            nxt = coded[y + 1]
            for x, v in enumerate(nxt):
                if v and not skin(v) and any(skin(nxt[k]) for k in range(max(0, x - 3), x)) and \
                        any(skin(nxt[k]) for k in range(x + 1, min(len(nxt), x + 4))):
                    hits.append((x, y + 1))
        if len(hits) >= 2:
            return (sum(h[0] for h in hits) // len(hits), sum(h[1] for h in hits) // len(hits))
    return None


HAIR = 0x80


def hair_mask(coded, eye_y):
    """Marks the hairstyle (bit 0x80) by shape, not colour (player,
    2026-11-27: the colour mask "masks out a lot of the actual hair"):
    everything above the brows (two rows over the eyes: crown, highlights,
    ribbons, hats), the hair colours anywhere, and whatever touches those
    (curtains, tails, their outline). The face and shoulders below stay out."""
    h, w = len(coded), len(coded[0])
    keep = [[bool(v) and (y < eye_y - 2 or v in (2, 3, 4)) for x, v in enumerate(row)] for y, row in enumerate(coded)]
    for _ in range(2):                     # grow onto touching non-skin pixels
        grow = [r[:] for r in keep]
        for y in range(h):
            for x in range(w):
                v = coded[y][x]
                if keep[y][x] or not v or v in (5, 6, 7, 15):
                    continue
                if any(0 <= y + dy < h and 0 <= x + dx < w and keep[y + dy][x + dx]
                       for dy in (-1, 0, 1) for dx in (-1, 0, 1)):
                    grow[y][x] = True
        keep = grow
    return [[v | (HAIR if keep[y][x] else 0) for x, v in enumerate(row)] for y, row in enumerate(coded)]


def build(gfx):
    heads = []
    for kind in SOURCES:
        if KIND_IDS.get(kind, kind) in EXCLUDE:
            continue
        try:
            images = [head_image(gfx, kind, d) for d in DIRS]
        except (IndexError, KeyError, ValueError, struct.error, TypeError):
            continue
        if any(im is None for im in images):
            continue
        palette = [tuple(c) for c in gfx.palette(kind)]
        role, ramp, extras, skin, hair = roles(palette, images)
        down = images[1]
        skin_px = sum(1 for row in down for v in row if v and role.get(v) in (5, 6, 7))
        hair_px = sum(1 for row in down for v in row if v and role.get(v) in (2, 3, 4))
        rows = [y for y, row in enumerate(down) if any(row)]
        if skin_px < 10 or hair_px < 10 or len(ramp) < 3 or len(rows) < 10:
            continue                       # not a person: no face, or no hair
        coded = [[[role.get(v, 0) if v else 0 for v in row] for row in im] for im in images]
        if any(c == coded[1] for c in (h[3][1] for h in heads)):
            continue                       # the same head again (recoloured copies are kept)
        eyes = [eye_centre(c) for c in coded]
        if eyes[1] is None:                # hair over the face: centre, above the chin
            chin = max((y for y, row in enumerate(coded[1]) for v in row if v in (5, 6, 7)), default=12)
            eyes[1] = (8, max(0, chin - 3))
        eyes[0] = None                     # from behind: no eyes, placed as from the front
        coded = [hair_mask(c, (e or eyes[1])[1]) for c, e in zip(coded, eyes)]
        heads.append((KIND_IDS.get(kind, kind), ramp, extras, coded, eyes))
    return heads


def pack(heads):
    out = bytearray(b"SOMHEAD3") + struct.pack("<H", len(heads))
    for kind, ramp, extras, coded, eyes in heads:
        out.append(kind & 0xFF)
        for c in ramp:
            out += bytes(c)
        out.append(len(extras))
        for k in range(8):
            out += bytes(extras[k]) if k < len(extras) else b"\0\0\0"
        for e in eyes:
            out += bytes(e) if e else b"\xff\xff"
        for im in coded:
            for row in im:
                out += bytes(row)
    return bytes(out)


def sheet(heads, path, scale=4):
    hero_skin = [(139, 82, 57), (213, 123, 82), (246, 189, 123)]
    cols, cell = 12, SIZE + 4
    rows = (len(heads) + cols - 1) // cols
    w, h = cols * cell * 3, rows * (cell + 2)
    pix = [[(30, 40, 44)] * (w * scale) for _ in range(h * scale)]
    for n, (kind, ramp, extras, coded, eyes) in enumerate(heads):
        for d, im in enumerate(coded):
            gx, gy = ((n % cols) * 3 + d) * cell, (n // cols) * (cell + 2)
            for y, row in enumerate(im):
                for x, v in enumerate(row):
                    if not v & HAIR:
                        continue
                    v &= 0x7F
                    c = ((49, 41, 32) if v == 1 else ramp[v - 2] if v <= 4 else hero_skin[v - 5] if v <= 7
                         else (250, 250, 250) if v == 15 else extras[v - 16])
                    for dy in range(scale):
                        pix[(gy + y) * scale + dy][(gx + x) * scale:(gx + x + 1) * scale] = [c] * scale
    raw = b"".join(b"\0" + bytes(v for p in r for v in p) for r in pix)
    def chunk(t, c):
        return struct.pack(">I", len(c)) + t + c + struct.pack(">I", zlib.crc32(t + c) & 0xFFFFFFFF)
    Path(path).write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w * scale, h * scale, 8, 2, 0, 0, 0))
                           + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("rom")
    ap.add_argument("out")
    ap.add_argument("--sheet", action="store_true")
    a = ap.parse_args()
    heads = build(H.HeroGfx(Path(a.rom).read_bytes()))
    if a.sheet:
        sheet(heads, a.out)
    else:
        Path(a.out).write_bytes(pack(heads))
    print("%d heads: %s" % (len(heads), " ".join("%02X" % (k & 0xFF) for k, *_ in heads)), file=sys.stderr)
    missing = ["%02X-side" % (h[0] & 0xFF) for h in heads if h[4][2] is None]
    if missing:
        print("no eyes in some views (placed from the front): " + " ".join(missing), file=sys.stderr)


if __name__ == "__main__":
    main()

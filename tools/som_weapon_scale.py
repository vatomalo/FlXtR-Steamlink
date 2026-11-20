#!/usr/bin/env python3
"""Scaled weapon art for the class weapon styles (player's design, 2026-11-20):
the same sword drawn short (Kunai, Dagger, Tanto) or long (Dai Katana,
Claymore). For every frame the swing scripts draw with a weapon, the weapon's
own list-2 pieces (blade, swing arc and flashes, tools/som_hero_gfx.py) are
rendered as the game draws them, then scaled about the hilt (the weapon
pixel nearest the hero's hands) with nearest-neighbour scaling so pixel art
stays crisp. The proxy hides the native weapon tiles while a scaled style is
held and draws these frames instead, picked by the hero's frame index (+0x26
/ 2) and mirrored for left facings (+0x28 bit 6, som-decomp graphics.md 4).

Pack: SOMWORK1 (tools/som_villager_work.py format), one entry per frame:
kind 0xD0 + scale index (SCALES), weapon = weapon row (type * 9 + grade),
facing = frame index, one image; colours 1-15 the hero's, 16-20 the blade's.

    python3 som_weapon_scale.py ROM OUT.bin
"""
import argparse
import math
import struct

import som_hero_gfx as H
import som_villager_work as V

KIND_BASE = 0xD0
SCALES = (55, 75, 150, 200)          # percent; 100 is the game's own art
ROWS = tuple(1 * 9 + grade for grade in (2, 3, 4, 5))   # the class swords' grades
CANVAS, ORIGIN = 224, (112, 140)      # room for a 200 % blade around the hero
HAND = (8, -14)                       # hands, from the foot point (px)


def weapon_frames(gfx, wtype):
    """Frame indices the weapon type's attack scripts draw (any facing)."""
    frames = set()
    for anim in range(64):
        for direction in (0, 1, 2):
            try:
                steps = gfx.script(wtype, anim, direction)
            except (IndexError, KeyError, ValueError, struct.error):
                continue
            frames.update(frame for _, frame, _ in steps)
    return sorted(frames)


def render_weapon(gfx, row, frame):
    tiles = gfx.weapon_tiles(row)
    pieces = gfx.weapon_pieces(row, frame)
    if not pieces:
        return None
    image = [[0] * CANVAS for _ in range(CANVAS)]
    gfx.render("boy", pieces, image=image, origin=ORIGIN, source=tiles, base=15)
    return image if any(any(line) for line in image) else None


def scale_about_hilt(image, percent):
    """`image` scaled by percent about its pixel nearest the hands."""
    hx, hy = ORIGIN[0] + HAND[0], ORIGIN[1] + HAND[1]
    pixels = [(x, y) for y, line in enumerate(image) for x, c in enumerate(line) if c]
    px, py = min(pixels, key=lambda p: (p[0] - hx) ** 2 + (p[1] - hy) ** 2)
    k = percent / 100.0
    out = [[0] * CANVAS for _ in range(CANVAS)]
    xs = [x for x, _ in pixels]
    ys = [y for _, y in pixels]
    for ny in range(max(0, int(py + (min(ys) - py) * k) - 1),
                    min(CANVAS, int(py + (max(ys) - py) * k) + 2)):
        sy = int(math.floor(py + (ny - py) / k + 0.5))
        if not 0 <= sy < CANVAS:
            continue
        for nx in range(max(0, int(px + (min(xs) - px) * k) - 1),
                        min(CANVAS, int(px + (max(xs) - px) * k) + 2)):
            sx = int(math.floor(px + (nx - px) / k + 0.5))
            if 0 <= sx < CANVAS and image[sy][sx]:
                out[ny][nx] = image[sy][sx]
    return out


def crop(image):
    """(dx, dy, w, h, bytes) relative to the foot point, as V.crop."""
    ys = [y for y, line in enumerate(image) if any(line)]
    xs = [x for x in range(CANVAS) if any(line[x] for line in image)]
    if not ys:
        return 0, 0, 0, 0, b""
    x0, x1, y0, y1 = xs[0], xs[-1] + 1, ys[0], ys[-1] + 1
    data = bytes(p for line in image[y0:y1] for p in line[x0:x1])
    return x0 - ORIGIN[0], y0 - ORIGIN[1] - V.HERO_HEIGHT, x1 - x0, y1 - y0, data


def pack(gfx, rows=ROWS, scales=SCALES):
    out, entries = bytearray(), 0
    hero = [V.bgr555(c) for c in gfx.palette("boy")[1:16]]
    for row in rows:
        wtype = row // 9
        blade = [V.bgr555(c) for c in gfx.weapon_palette(gfx.weapon_group(row)[1][4])[1:6]]
        for frame in weapon_frames(gfx, wtype):
            image = render_weapon(gfx, row, frame)
            if image is None:
                continue
            for index, percent in enumerate(scales):
                dx, dy, w, h, data = crop(scale_about_hilt(image, percent))
                if not w or not -128 <= dx <= 127 or not -128 <= dy <= 127 or w > 255 or h > 255:
                    continue
                out += struct.pack("<4B20H", KIND_BASE + index, row, frame, 1, *(hero + blade))
                out += struct.pack("<BbbBB", 1, dx, dy, w, h) + data
                entries += 1
    return b"SOMWORK1" + struct.pack("<H", entries) + bytes(out)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("rom")
    parser.add_argument("out")
    args = parser.parse_args()
    data = pack(H.HeroGfx.load(args.rom))
    with open(args.out, "wb") as f:
        f.write(data)
    print(f"{len(data)} bytes, {struct.unpack_from('<H', data, 8)[0]} entries -> {args.out}")


if __name__ == "__main__":
    main()

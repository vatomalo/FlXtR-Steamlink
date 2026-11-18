"""Cartwheel frames for the evade, made from the heroes' own standing frames.

Seen from the front, a cartwheel is the body turning in the screen plane,
so each step is a hero's standing frame rotated by a quarter turn about the
middle of the body: 0, 90, 180 (the handstand), 270 degrees, then upright
again. Quarter turns of pixel art are exact, so no pixels are invented.

    python3 som_cartwheel.py ROM OUT.bin      # the proxy pack (SOMWORK1)
    python3 som_cartwheel.py ROM OUT.png --sheet

Pack entries use the npc_work format (tools/som_villager_work.py) with kind
0x80 + hero (boy, girl, sprite) and weapon 0xFE. Facing 2 (side) is the
front frame turning clockwise: the head leads to the right, and the proxy
mirrors it for a cartwheel to the left. Facing 1 (down) is the side frame
turning, for cartwheels up or down the screen; facing 0 repeats it.
"""
import argparse
import struct

import som_hero_gfx as H
import som_villager_work as V

KIND_BASE = 0x80
WEAPON = 0xFE
HEROES = ("boy", "girl", "sprite")
TURNS = (0, 1, 2, 3, 0)         # quarter turns clockwise per step
TICKS = 1                       # one actor tick (5 frames) per step


def rotate(image, turns, pivot):
    """`image` turned `turns` quarter turns clockwise about pivot (x, y)."""
    size = len(image)
    out = [[0] * size for _ in range(size)]
    px, py = pivot
    for y, line in enumerate(image):
        for x, c in enumerate(line):
            if not c:
                continue
            dx, dy = x - px, y - py
            for _ in range(turns):
                dx, dy = -dy, dx
            nx, ny = px + dx, py + dy
            if 0 <= nx < size and 0 <= ny < size:
                out[ny][nx] = c
    return out


def body_centre(image):
    ys = [y for y, line in enumerate(image) if any(line)]
    xs = [x for x in range(len(image)) if any(line[x] for line in image)]
    return (xs[0] + xs[-1]) // 2, (ys[0] + ys[-1]) // 2


def cartwheel(gfx, hero, source="down"):
    """[(image, ticks)] of one cartwheel from the `source` standing frame."""
    still = gfx.render(hero, V.standing(gfx, hero, source), size=V.CELL, origin=V.ORIGIN)
    pivot = body_centre(still)
    return [(rotate(still, t, pivot), TICKS) for t in TURNS]


def pack(gfx):
    out, entries = bytearray(), 0
    for i, hero in enumerate(HEROES):
        colours = [V.bgr555(c) for c in gfx.palette(hero)[1:16]] + [0] * 5
        for facing, source in ((0, "side"), (1, "side"), (2, "down")):
            frames = cartwheel(gfx, hero, source)
            out += struct.pack("<4B20H", KIND_BASE + i, WEAPON, facing, len(frames), *colours)
            for image, ticks in frames:
                dx, dy, w, h, data = V.crop(image)
                out += struct.pack("<BbbBB", ticks, dx, dy, w, h) + data
            entries += 1
    return b"SOMWORK1" + struct.pack("<H", entries) + bytes(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("rom")
    ap.add_argument("out")
    ap.add_argument("--sheet", action="store_true", help="write a PNG sheet instead")
    args = ap.parse_args()
    gfx = H.HeroGfx.load(args.rom)
    if not args.sheet:
        data = pack(gfx)
        with open(args.out, "wb") as f:
            f.write(data)
        print(f"{len(data)} bytes -> {args.out}")
        return
    rows, palette = [], []
    for i, hero in enumerate(HEROES):
        palette += gfx.palette(hero)
        for source in ("down", "side"):
            frames = cartwheel(gfx, hero, source)
            for y in range(V.CELL):
                line = []
                for image, _ in frames:
                    line += [p + 16 * i if p else 0 for p in image[y]]
                rows.append(line)
    H.write_png(args.out, rows, palette, scale=2)


if __name__ == "__main__":
    main()

"""Working villagers: a villager's head on a hero's swing, in the villager's colours.

Villager sprites only have standing and walking frames, so a villager at
work borrows a hero attack: each frame of e.g. the boy's axe swing is drawn
with the boy's head row (the top row of pieces) replaced by the villager's
head row, which follows the boy's head as he lunges. The boy's body keeps
its shapes but takes the villager's colours: the boy's and the villager's
standing frames share a foot point, and every boy colour is mapped to the
villager colour it overlaps most (colours that never overlap take the
nearest villager colour). The weapon (frame list 2) is drawn on top with
its own five colours, still unchecked against a live game (see
som_hero_gfx).

    python3 som_villager_work.py ROM OUT.png [--weapon axe --row 18] KIND...

draws one row per villager kind (hex, e.g. c0 c1) and one column per step.

    python3 som_villager_work.py ROM OUT.bin --pack [--all | KIND...]

writes the frame pack the proxy draws from (src/som_npc_work.h), for every
facing (0 up, 1 down, 2 side = right; the proxy mirrors it for left):

    "SOMWORK1", u16 entries, then per entry
      u8 kind, u8 weapon type, u8 facing, u8 frames, 20 x u16 BGR555 colours
      (1-15 the villager's, 16-20 the weapon's), then per frame
      u8 ticks, i8 dx, i8 dy, u8 w, u8 h, w*h colour indices (0 transparent),
    dx/dy = the top-left pixel relative to the foot point (the body is the
    hero's, so the hero's height HERO_HEIGHT is used, not the villager's).
"""
import argparse
import collections
import struct

import som_hero_gfx as H

CELL, ORIGIN = 96, (40, 60)   # wide enough for the axe's forward and low frames
HERO_HEIGHT = 14              # +0x74 & 0x7F of the three heroes (docs/graphics.md 5)
FACINGS = ("up", "down", "side")


def standing(gfx, kind, direction="side"):
    table = gfx.actor(kind)["ordinary"]
    flags, frame, _ = gfx.script(None, 0, direction, attack=False, table=table)[0]
    return gfx.pieces(kind, None, frame, flags)


def colour_map(gfx, kind, hero="boy"):
    """{hero colour index: villager colour index} for indices 1-15."""
    a = gfx.render(hero, standing(gfx, hero), size=CELL, origin=ORIGIN)
    b = gfx.render(kind, standing(gfx, kind), size=CELL, origin=ORIGIN)
    votes = collections.defaultdict(collections.Counter)
    for row_a, row_b in zip(a, b):
        for p, q in zip(row_a, row_b):
            if p and q:
                votes[p][q] += 1
    hp, vp = gfx.palette(hero), gfx.palette(kind)
    def nearest(i):
        return min(range(1, 16), key=lambda j: sum((x - y) ** 2 for x, y in zip(hp[i], vp[j])))
    return {i: votes[i].most_common(1)[0][0] if votes[i] else nearest(i) for i in range(1, 16)}


def head_row(pieces):
    top = min(p[1] for p in pieces)
    head = [p for p in pieces if p[1] == top]
    return top, sum(p[0] for p in head) // len(head), head


def work_frames(gfx, kind, weapon="axe", row=18, hero="boy", anim=0, direction="side",
                weapon_base=16):
    """[(colour-index image CELLxCELL, ticks)] of the swing. Villager colours
    are 1-15 (its palette), weapon colours weapon_base + 1-5."""
    cmap = colour_map(gfx, kind, hero)
    _, vx, vhead = head_row(standing(gfx, kind, direction))
    vtop = min(p[1] for p in vhead)
    tiles = gfx.weapon_tiles(row)
    frames = []
    for flags, frame, ticks in gfx.script(weapon, anim, direction):
        pieces = gfx.pieces(hero, weapon, frame, flags)
        htop, hx, _ = head_row(pieces)
        body = gfx.render(hero, [p for p in pieces if p[1] != htop], size=CELL, origin=ORIGIN)
        image = [[cmap[p] if p else 0 for p in line] for line in body]
        gfx.render(hero, gfx.weapon_pieces(row, frame, flags), image=image, origin=ORIGIN,
                   source=tiles, base=weapon_base)
        moved = [(x - vx + hx, y - vtop + htop, hf, vf, t) for x, y, hf, vf, t in vhead]
        gfx.render(kind, moved, image=image, origin=ORIGIN)
        frames.append((image, ticks))
    return frames


def humanoid(gfx, kind):
    """True for an object kind that stands like a person: a decodable standing
    frame of two piece rows, 24-40 px tall."""
    try:
        pieces = standing(gfx, kind)
    except (IndexError, struct.error, ValueError):
        return False
    rows = {p[1] for p in pieces}
    if len(rows) < 2 or len(pieces) > 6:
        return False
    return 24 <= max(rows) + 16 - min(rows) <= 40


def bgr555(colour):
    r, g, b = (c * 31 // 255 for c in colour)
    return r | g << 5 | b << 10


def crop(image):
    """(dx, dy, w, h, bytes) of the drawn pixels, relative to the foot point."""
    ys = [y for y, line in enumerate(image) if any(line)]
    xs = [x for x in range(CELL) if any(line[x] for line in image)]
    if not ys:
        return 0, 0, 0, 0, b""
    x0, x1, y0, y1 = xs[0], xs[-1] + 1, ys[0], ys[-1] + 1
    data = bytes(p for line in image[y0:y1] for p in line[x0:x1])
    return x0 - ORIGIN[0], y0 - ORIGIN[1] - HERO_HEIGHT, x1 - x0, y1 - y0, data


def pack(gfx, kinds, weapon="axe", row=18):
    wtype = H.WEAPON_TYPES.index(weapon)
    wpal = gfx.weapon_palette(gfx.weapon_group(row)[1][4])[1:6]
    out, entries = bytearray(), 0
    for kind in kinds:
        colours = [bgr555(c) for c in gfx.palette(kind)[1:16] + wpal]
        for facing, name in enumerate(FACINGS):
            frames = work_frames(gfx, kind, weapon, row, direction=name)
            out += struct.pack("<4B20H", kind, wtype, facing, len(frames), *colours)
            for image, ticks in frames:
                dx, dy, w, h, data = crop(image)
                out += struct.pack("<BbbBB", ticks, dx, dy, w, h) + data
            entries += 1
    return b"SOMWORK1" + struct.pack("<H", entries) + bytes(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("rom")
    ap.add_argument("out")
    ap.add_argument("kinds", nargs="*", help="villager object kinds, hex")
    ap.add_argument("--pack", action="store_true", help="write the proxy frame pack")
    ap.add_argument("--all", action="store_true", help="every humanoid kind 0x83-0xFF")
    ap.add_argument("--weapon", default="axe")
    ap.add_argument("--row", type=int, default=18, help="weapon row (D0:1000 + 12 * row)")
    ap.add_argument("--scale", type=int, default=3)
    args = ap.parse_args()
    gfx = H.HeroGfx.load(args.rom)
    kinds = [int(k, 16) for k in args.kinds]
    if args.all:
        kinds = [k for k in range(0x83, 0x100) if humanoid(gfx, k)]
    if not kinds:
        ap.error("give villager kinds or --all")
    if args.pack:
        data = pack(gfx, kinds, args.weapon, args.row)
        with open(args.out, "wb") as f:
            f.write(data)
        print(f"{len(kinds)} kinds, {len(data)} bytes -> {args.out}")
        return
    wbase = 16 * len(kinds)
    palette = [c for k in kinds for c in gfx.palette(k)]
    palette += gfx.weapon_palette(gfx.weapon_group(args.row)[1][4])
    sheet = None
    for i, kind in enumerate(kinds):
        frames = work_frames(gfx, kind, args.weapon, args.row, weapon_base=wbase)
        if sheet is None:
            sheet = [[0] * (CELL * len(frames)) for _ in range(CELL * len(kinds))]
        for c, (image, _) in enumerate(frames):
            for y, line in enumerate(image):
                for x, p in enumerate(line):
                    if p:
                        sheet[i * CELL + y][c * CELL + x] = p if p >= wbase else p + 16 * i
    H.write_png(args.out, sheet, palette, scale=args.scale)


if __name__ == "__main__":
    main()

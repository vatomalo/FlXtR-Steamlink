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
"""
import argparse
import collections

import som_hero_gfx as H

CELL, ORIGIN = 96, (40, 60)   # wide enough for the axe's forward and low frames


def standing(gfx, kind):
    table = gfx.actor(kind)["ordinary"]
    flags, frame, _ = gfx.script(None, 0, "side", attack=False, table=table)[0]
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
    _, vx, vhead = head_row(standing(gfx, kind))
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


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("rom")
    ap.add_argument("out")
    ap.add_argument("kinds", nargs="+", help="villager object kinds, hex")
    ap.add_argument("--weapon", default="axe")
    ap.add_argument("--row", type=int, default=18, help="weapon row (D0:1000 + 12 * row)")
    ap.add_argument("--scale", type=int, default=3)
    args = ap.parse_args()
    gfx = H.HeroGfx.load(args.rom)
    kinds = [int(k, 16) for k in args.kinds]
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

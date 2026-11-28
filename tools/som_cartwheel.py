"""Hero move frames for the proxy: the cartwheel evade and the flying kick.

Seen from the front, a cartwheel is the body turning in the screen plane,
so each step is a hero's standing frame rotated by a quarter turn about the
middle of the body: 0, 90, 180 (the handstand), 270 degrees, then upright
again. Quarter turns of pixel art are exact, so no pixels are invented.

    python3 som_cartwheel.py ROM OUT.bin      # the proxy pack (SOMWORK1)
    python3 som_cartwheel.py ROM OUT.png --sheet

The flying kick (Martial Arts, kinds 0x90 + hero, weapon 0xFD) is the
glove's own kick from the pose table D0:FFF0, the chamber for one tick and
the kick held: side 88 -> 89, down 90 -> 91, up 92 -> 93 (the glove's attack
2 in each direction).

Cartwheel pack entries use the npc_work format (tools/som_villager_work.py) with kind
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
UKEMI_KIND_BASE = 0xCC
UKEMI_WEAPON = 0xFA
# Ukemi uses the crouch and four directional tumble frames from pose 0x9E.
# Durations are video frames for this entry, at the roll's reference speed.
UKEMI_FRAMES = {"up": (47, 142, 143, 144, 145, 47),
                "down": (49, 138, 139, 140, 141, 49),
                "side": (45, 134, 135, 136, 137, 45)}
UKEMI_TIMING = (2, 3, 3, 3, 3, 4)
# Forward cartwheel (Swordsmanship, forward + A, 2026-11-20): the game's own
# cartwheel (miss reaction 0x93: frames 65, 66, 67, 66 turned over, 65)
# travels away from the facing; drawn as it is while the hero moves toward
# his facing, he dashes forward facing forward. Kinds 0xC8 + hero, side only.
FORWARD_KIND_BASE = 0xC8
FORWARD_WEAPON = 0xF9
# Side (player, 2026-11-28: "make the swordman cartwheel jump to the sides
# with an animation equivalent to the forwards and back"): a jump like the
# up and down ones, the side crouch 45 and the airborne 46 (both face right),
# no longer the game's sideways flip 65-67.
FORWARD_FRAMES = ((0, 45), (0, 46), (0, 46), (0, 46), (0, 45))
# The game's own back and front cartwheel (pose 0x93 facing up and down), for
# maps where it refuses the reaction (2026-11-25). Facing 0 up, 1 down, 2 side.
FORWARD_BY_FACING = {0: ((0, 47), (0, 42), (0, 48), (0, 42), (0, 47)),
                     1: ((0, 49), (0, 50), (0, 50), (0, 50), (0, 49)),
                     2: FORWARD_FRAMES}
HEROES = ("boy", "girl", "sprite")
TURNS = (0, 1, 2, 3, 0)         # quarter turns clockwise per step
TICKS = 1                       # one actor tick (5 frames) per step
KICK_KIND_BASE = 0x90
KICK_WEAPON = 0xFD
KICK_FRAMES = {0: (92, 93), 1: (90, 91), 2: (88, 89)}   # facing: chamber, kick
KICK_TICKS = (1, 4)
# Fuma (Ninjutsu boomerang): a shadow clone's sword swing, kinds 0xA0 + hero,
# weapon 0xFC: the hero's own sword attack (anim 0) with the blade drawn over
# it (colours 16-20, sword row 9), facings 0 up, 1 down, 2 side.
FUMA_KIND_BASE = 0xA0
FUMA_WEAPON = 0xFC
SWORD_ROW = 9
# Companion (test team-mate, src/som_ai_proxy.c companion_<class>): the
# girl's body with a villager's head and colours (the working-villager
# custom parts, som_villager_work), kind 0xB0 = her walk (ordinary anim 1),
# 0xB1 + weapon type = her swing with that weapon (tier-0 row = type * 9).
# The proxy recolours 1-15 with her class palette (som_class_palette.h).
COMPANION_BODY = "girl"
COMPANION_HEAD = 0x8C
COMPANION_WALK = 0xB0
COMPANION_SWING = 0xB1


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


def somersault(gfx, hero, direction):
    """[(image, ticks)] of the Ukemi roll toward `direction` (up, down, side)."""
    images = [gfx.render(hero, gfx.pieces(hero, None, frame),
                         size=V.CELL, origin=V.ORIGIN)
              for frame in UKEMI_FRAMES[direction]]
    ground = max(y for y, row in enumerate(images[0]) if any(row))
    frames = []
    for image, duration in zip(images, UKEMI_TIMING):
        bottom = max(y for y, row in enumerate(image) if any(row))
        offset = ground - bottom
        aligned = [[0] * V.CELL for _ in range(V.CELL)]
        for y, row in enumerate(image):
            if 0 <= y + offset < V.CELL:
                aligned[y + offset] = row
        frames.append((aligned, duration))
    return frames


def forward_cartwheel(gfx, hero, facing=2):
    """[(image, ticks)] of the game's cartwheel frames, facing right."""
    return [(gfx.render(hero, gfx.pieces(hero, None, frame, flags),
                        size=V.CELL, origin=V.ORIGIN), 1)
            for flags, frame in FORWARD_BY_FACING[facing]]


def flying_kick(gfx, hero, frame_ids):
    return [(gfx.render(hero, gfx.pieces(hero, None, f), size=V.CELL, origin=V.ORIGIN), t)
            for f, t in zip(frame_ids, KICK_TICKS)]


def sword_swing(gfx, hero, direction, row=SWORD_ROW):
    """[(image, ticks)] of the hero's plain sword swing, blade included."""
    tiles = gfx.weapon_tiles(row)
    frames = []
    for flags, frame, ticks in gfx.script("sword", 0, direction):
        image = gfx.render(hero, gfx.pieces(hero, "sword", frame, flags),
                           size=V.CELL, origin=V.ORIGIN)
        gfx.render(hero, gfx.weapon_pieces(row, frame, flags), image=image,
                   origin=V.ORIGIN, source=tiles, base=16)
        frames.append((image, ticks))
    return frames


def companion_walk(gfx, direction, body=COMPANION_BODY, head=COMPANION_HEAD):
    """[(image, ticks)] of the body's walk with the villager's head and colours."""
    cmap = V.colour_map(gfx, head, body)
    _, vx, vhead = V.head_row(V.standing(gfx, head, direction))
    vtop = min(piece[1] for piece in vhead)
    table = gfx.actor(body)["ordinary"]
    frames = []
    for flags, frame, ticks in gfx.script(None, 1, direction, attack=False, table=table):
        pieces = gfx.pieces(body, None, frame, flags)
        htop, hx, _ = V.head_row(pieces)
        image = gfx.render(body, [q for q in pieces if q[1] != htop], size=V.CELL,
                           origin=V.ORIGIN)
        image = [[cmap[q] if q else 0 for q in line] for line in image]
        moved = [(x - vx + hx, y - vtop + htop, hf, vf, t) for x, y, hf, vf, t in vhead]
        gfx.render(head, moved, image=image, origin=V.ORIGIN)
        frames.append((image, ticks))
    return frames


def companion_entries(gfx, body=COMPANION_BODY, head=COMPANION_HEAD):
    """[(kind, facing, frames, colours)] for the companion pack entries."""
    villager = [V.bgr555(c) for c in gfx.palette(head)[1:16]]
    out = []
    for facing, direction in enumerate(V.FACINGS):
        out.append((COMPANION_WALK, facing, companion_walk(gfx, direction, body, head),
                    villager + [0] * 5))
    for wtype, weapon in enumerate(H.WEAPON_TYPES):
        row = wtype * 9
        try:
            blade = [V.bgr555(c) for c in
                     gfx.weapon_palette(gfx.weapon_group(row)[1][4])[1:6]]
            for facing, direction in enumerate(V.FACINGS):
                frames = V.work_frames(gfx, head, weapon, row, hero=body, direction=direction)
                out.append((COMPANION_SWING + wtype, facing, frames, villager + blade))
        except (IndexError, KeyError, ValueError, struct.error):
            continue                 # a weapon this body has no swing for
    return out


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
        for facing, direction in enumerate(V.FACINGS):
            frames = somersault(gfx, hero, direction)
            out += struct.pack("<4B20H", UKEMI_KIND_BASE + i, UKEMI_WEAPON, facing,
                               len(frames), *colours)
            for image, ticks in frames:
                dx, dy, w, h, data = V.crop(image)
                out += struct.pack("<BbbBB", ticks, dx, dy, w, h) + data
            entries += 1
        for facing in sorted(FORWARD_BY_FACING):
            frames = forward_cartwheel(gfx, hero, facing)
            out += struct.pack("<4B20H", FORWARD_KIND_BASE + i, FORWARD_WEAPON, facing,
                               len(frames), *colours)
            for image, ticks in frames:
                dx, dy, w, h, data = V.crop(image)
                out += struct.pack("<BbbBB", ticks, dx, dy, w, h) + data
            entries += 1
        for facing, frame_ids in KICK_FRAMES.items():
            frames = flying_kick(gfx, hero, frame_ids)
            out += struct.pack("<4B20H", KICK_KIND_BASE + i, KICK_WEAPON, facing, len(frames),
                               *colours)
            for image, ticks in frames:
                dx, dy, w, h, data = V.crop(image)
                out += struct.pack("<BbbBB", ticks, dx, dy, w, h) + data
            entries += 1
        blade = [V.bgr555(c) for c in
                 gfx.weapon_palette(gfx.weapon_group(SWORD_ROW)[1][4])[1:6]]
        for facing, direction in enumerate(V.FACINGS):
            frames = sword_swing(gfx, hero, direction)
            out += struct.pack("<4B20H", FUMA_KIND_BASE + i, FUMA_WEAPON, facing, len(frames),
                               *(colours[:15] + blade))
            for image, ticks in frames:
                dx, dy, w, h, data = V.crop(image)
                out += struct.pack("<BbbBB", ticks, dx, dy, w, h) + data
            entries += 1
    for kind, facing, frames, colours in companion_entries(gfx):
        out += struct.pack("<4B20H", kind, 0xFB, facing, len(frames), *colours)
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
            if source == "side":
                frames += [im for ids in KICK_FRAMES.values() for im in flying_kick(gfx, hero, ids)]
            for y in range(V.CELL):
                line = []
                for image, _ in frames:
                    line += [p + 16 * i if p else 0 for p in image[y]]
                rows.append(line)
    width = max(len(line) for line in rows)
    rows = [line + [0] * (width - len(line)) for line in rows]
    H.write_png(args.out, rows, palette, scale=2)


if __name__ == "__main__":
    main()

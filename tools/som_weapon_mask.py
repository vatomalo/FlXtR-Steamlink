#!/usr/bin/env python3
"""Bushido sword masks built from the player's own Secret of Mana Turbo ROM.

Tanto: remove only the distal part of the blade. Dai Katana: extend its
one/two-pixel edge and rebuild the tip. Katana stays native. The grip,
guard, hero, and slash animation are NEVER resized or translated.

A frame is accepted only when a blade can be identified near the hero's
actual pixels. Unsafe/ambiguous frames are omitted: the runtime restores
native weapon art instead of displaying a detached or invisible weapon.

SOMWORK1: one packed, cropped colour-index image per (style, row, frame).
Pixels 1..5 = opaque weapon palette, 6..10 = 25%-opacity FX pixels.
Kinds 0xD4 Tanto and 0xD5 Dai Katana, weapon = native row, facing = frame.
The pack is generated on the user's build machine; no ROM is bundled.
"""
import argparse
import math
import struct

import som_hero_gfx as H
from som_donor_dagger import import_dagger, place_dagger
import som_villager_work as V

KIND_TANTO = 0xD4
KIND_DAI = 0xD5
SWORD_ROWS = tuple(range(9, 18))
CANVAS = 224
ORIGIN = (112, 140)
MAX_BLADE_EXTENSION = 14


def neighbours(x, y, width, height):
    for yy in range(max(0, y - 1), min(height, y + 2)):
        for xx in range(max(0, x - 1), min(width, x + 2)):
            if xx != x or yy != y:
                yield xx, yy


def components(image):
    """Eight-connected opaque islands: effects stay separate from the blade."""
    height, width = len(image), len(image[0])
    pending = {(x, y) for y, row in enumerate(image) for x, c in enumerate(row) if c}
    result = []
    while pending:
        start = pending.pop()
        group, stack = [start], [start]
        while stack:
            x, y = stack.pop()
            for q in neighbours(x, y, width, height):
                if q in pending:
                    pending.remove(q)
                    group.append(q)
                    stack.append(q)
        result.append(group)
    return result


def blade_geometry(image, body, foot=ORIGIN):
    """Return (connected blade mask, grip, tip, central-axis mask).

    The grip is chosen per frame from the weapon pixels nearest the visible
    lower hero torso/hand region, not a fixed screen offset. The blade must
    extend away from that grip along a reasonably narrow axis. Reject frames
    that look like a detached slash or a broad flash.
    """
    body_points = [(x, y) for y, row in enumerate(body) for x, c in enumerate(row)
                   if c and foot[1] - 38 <= y <= foot[1] and abs(x - foot[0]) <= 27]
    if not body_points:
        return None
    best = None
    for group in components(image):
        if len(group) < 7:
            continue
        grip, dist2 = None, 10**8
        for x, y in group:
            # The exact body/weapon overlap is used when it exists. Otherwise
            # we accept a 3px separation for the SNES sprite piece offsets.
            d = min((x - bx)**2 + (y - by)**2 for bx, by in body_points)
            if d < dist2:
                grip, dist2 = (x, y), d
        if dist2 > 9:
            continue
        gx, gy = grip
        tip = max(group, key=lambda p: (p[0] - gx)**2 + (p[1] - gy)**2)
        vx, vy = tip[0] - gx, tip[1] - gy
        reach = math.hypot(vx, vy)
        if reach < 7:
            continue
        ux, uy = vx / reach, vy / reach
        line = {(x, y) for x, y in group if
                0 <= (x-gx)*ux + (y-gy)*uy <= reach+1 and
                abs((x-gx)*uy - (y-gy)*ux) <= 2.5}
        # A blade should be thinner than a broad crescent slash or explosion.
        if len(line) < max(6, int(0.40*len(group))):
            continue
        score = 3*len(line) - len(group) - 6*dist2
        if best is None or score > best[0]:
            best = score, set(group), grip, tip, line
    return best[1:] if best else None


def masked_style(image, body, style, foot=ORIGIN, donor=None):
    """Opaque blade geometry + masked effects, or None for native fallback.

    Neither branch scales the original pixels. Pixels not in the identified
    distal blade are copied to the exact original coordinates.
    """
    if style not in ("tanto", "dai"):
        raise ValueError("only the two non-native Bushido styles are masked")
    found = blade_geometry(image, body, foot)
    if found is None:
        return None
    group, grip, tip, axis_mask = found
    gx, gy = grip
    vx, vy = tip[0]-gx, tip[1]-gy
    reach = math.hypot(vx, vy)
    ux, uy = vx/reach, vy/reach
    out = [row[:] for row in image]
    if style == "tanto":
        new_length = max(5.0, reach*0.54)
        for x, y in axis_mask:
            advance = (x-gx)*ux+(y-gy)*uy
            if advance > new_length and advance > 4:
                out[y][x] = 0
        if donor:
            # Authentic donor blade replaces only the original distal blade.
            # No native hilt, guard, or slash effect is stretched.
            for x, y in axis_mask:
                if (x-gx)*ux+(y-gy)*uy > 4:
                    out[y][x] = 0
            place_dagger(out, donor, grip, tip)
        else:
            # Safe procedural version until an SD3 dagger cutout is provided.
            tx, ty = round(gx+new_length*ux), round(gy+new_length*uy)
            if 0 <= tx < len(image[0]) and 0 <= ty < len(image):
                if (tx, ty) in group or out[ty][tx] == 0:
                    out[ty][tx] = image[tip[1]][tip[0]]
    else:
        extra = min(MAX_BLADE_EXTENSION, max(5, round(reach*0.50)))
        tip_colour = image[tip[1]][tip[0]]
        for step in range(1, extra+1):
            cx, cy = round(tip[0]+ux*step), round(tip[1]+uy*step)
            for thickness in (0, 1) if step < extra-2 else (0,):
                x, y = round(cx-uy*thickness), round(cy+ux*thickness)
                if 0 <= x < len(image[0]) and 0 <= y < len(image):
                    if out[y][x] == 0:  # never erase existing guard, FX or hilt
                        out[y][x] = tip_colour
    # Detached, distant effect islands get their OWN alpha mask. Connected
    # guard/blade pixels remain fully opaque and retain all palette colours.
    body_points = [(x, y) for y, row in enumerate(body) for x, c in enumerate(row) if c]
    for island in components(image):
        if any(p in group for p in island) or len(island) < 3:
            continue
        if body_points and min((x-bx)**2+(y-by)**2 for x,y in island
                               for bx,by in body_points) >= 10**2:
            for x, y in island:
                if 1 <= image[y][x] <= 5:
                    out[y][x] = image[y][x]+5
    return out


def sword_frames(gfx):
    """Stable original frame numbers; left mirrors at runtime as before."""
    out = {}
    for anim in range(64):
        for direction in V.FACINGS:
            try:
                for flags, frame, _ in gfx.script("sword", anim, direction):
                    out.setdefault(frame, flags)
            except (IndexError, KeyError, ValueError, struct.error):
                continue
    return sorted(out.items())


def cropped(image):
    filled = [(x, y) for y, line in enumerate(image) for x, c in enumerate(line) if c]
    if not filled:
        return None
    x0, x1 = min(p[0] for p in filled), max(p[0] for p in filled)+1
    y0, y1 = min(p[1] for p in filled), max(p[1] for p in filled)+1
    dx, dy = x0-ORIGIN[0], y0-ORIGIN[1]-V.HERO_HEIGHT
    if not -128 <= dx <= 127 or not -128 <= dy <= 127 or x1-x0 > 255 or y1-y0 > 255:
        return None
    return dx, dy, x1-x0, y1-y0, bytes(c for row in image[y0:y1] for c in row[x0:x1])


def pack(gfx, rows=SWORD_ROWS, dagger=None):
    entries, encoded = 0, bytearray()
    for row in rows:
        tiles = gfx.weapon_tiles(row)
        blade = [V.bgr555(c) for c in gfx.weapon_palette(gfx.weapon_group(row)[1][4])[1:6]]
        for frame, flags in sword_frames(gfx):
            wp = gfx.weapon_pieces(row, frame, flags)
            if not wp:
                continue
            weapon = [[0]*CANVAS for _ in range(CANVAS)]
            body = [[0]*CANVAS for _ in range(CANVAS)]
            gfx.render("boy", wp, image=weapon, origin=ORIGIN, source=tiles)
            gfx.render("boy", gfx.pieces("boy", "sword", frame, flags),
                       image=body, origin=ORIGIN)
            for style, kind in (("tanto", KIND_TANTO), ("dai", KIND_DAI)):
                art = masked_style(weapon, body, style, donor=dagger if style=="tanto" else None)
                record = cropped(art) if art else None
                if record is None:
                    continue
                dx, dy, w, h, pixels = record
                colours = [V.bgr555(c) for c in dagger["palette"]] if style=="tanto" and dagger else blade
                palette = colours + colours + [0]*10
                encoded += struct.pack("<4B20H", kind, row, frame, 1, *palette)
                encoded += struct.pack("<BbbBB", 1, dx, dy, w, h)+pixels
                entries += 1
    if entries > 65535:
        raise ValueError("too many weapon mask frames")
    return b"SOMWORK1"+struct.pack("<H", entries)+bytes(encoded)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("rom", help="local Turbo ROM, read-only")
    parser.add_argument("out", help="SOMWORK1 output file")
    parser.add_argument("--dagger-png", help="one cropped transparent SD3 dagger PNG")
    parser.add_argument("--dagger-hilt", help="opaque hilt pixel as X,Y in the cropped image")
    parser.add_argument("--dagger-tip", help="opaque tip pixel as X,Y in the cropped image")
    args = parser.parse_args()
    if bool(args.dagger_png) != bool(args.dagger_hilt and args.dagger_tip):
        parser.error("--dagger-png requires --dagger-hilt X,Y and --dagger-tip X,Y")
    def point(value):
        try:
            x,y=value.split(",")
            return int(x),int(y)
        except (ValueError,AttributeError):
            parser.error("use X,Y for donor anchor coordinates")
    donor = import_dagger(args.dagger_png,point(args.dagger_hilt),point(args.dagger_tip)) if args.dagger_png else None
    payload = pack(H.HeroGfx.load(args.rom), dagger=donor)
    with open(args.out, "wb") as out:
        out.write(payload)
    print(f"Bushido masks: {int.from_bytes(payload[8:10], 'little')} frames, "
          f"{len(payload)} bytes -> {args.out}")


if __name__ == "__main__":
    main()

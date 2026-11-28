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

Pack "SOMHAIRF3": u32 count, then entries sorted by style, facing, position,
flips and the FNV-1a 64 of a piece's four source tiles. Style 0 is bald;
styles 1..N follow the ROM head pack order. Each record stores 256 colour
indices (16 x 16, as stored, before the game flips the piece).

Each borrowed hairstyle is aligned independently on every extracted native
pose. Visible eye centres anchor front and side poses; native hair bounds
anchor back views. The host pose remains intact, and gameplay only looks up
the prepared result by source tile hash.
"""
import argparse
import struct
import sys
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import som_hero_gfx as H  # noqa: E402
import som_head_pack as HP  # noqa: E402

HAIR = {2, 3, 4}
SKIN = {5, 6, 7}
EYE = 15
SIZE, ORIGIN = 96, (48, 64)
HEAD_SCALE_MIN, HEAD_SCALE_MAX = 0.75, 1.25


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


def cut(gfx, pieces, image, pose):
    """Prepared pieces keyed by source pixels and pose-relative placement."""
    out = {}
    view, mirror, anchor, _ = pose
    for x, y, hf, vf, tiles in pieces:
        tile_hash = fnv64(b"".join(tile_bytes(gfx, t) for t in tiles))
        px, py = ORIGIN[0] + x, ORIGIN[1] + y
        key = (view, int(mirror), px - anchor[0], py - anchor[1], int(hf), int(vf), tile_hash)
        stored = []
        for v in range(16):
            for u in range(16):
                sx, sy = (15 - u if hf else u), (15 - v if vf else v)
                stored.append(image[py + sy][px + sx])
        out[key] = (bytes(stored), tiles)
    return out


def native_frames(gfx, hero):
    """Distinct native poses and their source pieces, rendered only once."""
    frames = []
    for layout, (weapon, frame) in frames_of(gfx, hero).items():
        pieces = [(x, y, hf, vf, list(t)) for x, y, hf, vf, t in layout]
        if any(not (0 <= ORIGIN[0] + x and ORIGIN[0] + x + 16 <= SIZE and 0 <= ORIGIN[1] + y and
                    ORIGIN[1] + y + 16 <= SIZE) for x, y, _, _, _ in pieces):
            continue                       # not a real frame (past the end of a table)
        native = gfx.render(hero, pieces, size=SIZE, origin=ORIGIN)
        frames.append((pieces, native, bald(native)))
    return frames


def _clusters(xs):
    count, previous = 0, -9
    for x in sorted(set(xs)):
        if x - previous > 2:
            count += 1
        previous = x
    return count


def target_head(native):
    """Find this pose's facing, eye anchor and complete hair silhouette.

    Palette indices 2-4 are the hair fill. Include only attached index-1
    outline pixels in the measured bounds so host and donor boxes both cover
    the full sprite, for each of the three ROM views (side mirrored to left).
    """
    hair = [(x, y) for y, row in enumerate(native) for x, c in enumerate(row) if c in HAIR]
    if not hair:
        return None
    shape = set(hair)
    for x, y in hair:
        shape.update((nx, ny) for nx, ny in ((x-1,y-1),(x,y-1),(x+1,y-1),
                                              (x-1,y),(x+1,y),
                                              (x-1,y+1),(x,y+1),(x+1,y+1))
                     if 0 <= nx < SIZE and 0 <= ny < SIZE and native[ny][nx] == 1)
    xs, ys = [p[0] for p in shape], [p[1] for p in shape]
    left, right, top = min(xs), max(xs), min(ys)
    cx = (left + right + 1) // 2
    eye_points = [(x, y) for y in range(max(0, top + 3), min(SIZE, top + 24))
                  for x in range(max(0, left - 4), min(SIZE, right + 5)) if native[y][x] == EYE]
    if not eye_points:
        return 0, False, (cx, top), (left, top, right, max(ys))
    ex = sum(x for x, _ in eye_points) // len(eye_points)
    ey = sum(y for _, y in eye_points) // len(eye_points)
    if _clusters(x for x, _ in eye_points) >= 2:
        return 1, False, (ex, ey), (left, top, right, max(ys))
    return 2, ex < cx, (ex, ey), (left, top, right, max(ys))


def _nearest_palette(rgb, palette):
    return min(range(1, 16), key=lambda i: sum((palette[i][k] - rgb[k]) ** 2 for k in range(3)))


def overlay_head(base, native, head, palette):
    """Precompose a matching-angle hairstyle against this pose's bare skull.

    Every native animation pose is aligned separately. The donor's three
    actual ROM views (back, front, profile) are selected independently; the
    profile is mirrored for the opposite facing. Scale is measured separately
    for width and height from each full hair silhouette, while the eyes anchor
    front/profile views. This avoids treating one angle as a universal fit.
    """
    target = target_head(native)
    if target is None:
        return [row[:] for row in base]
    view, mirror, target_anchor, bounds = target
    _, _, extras, coded, eyes = head
    source = coded[view]
    hair = [(x, y) for y, row in enumerate(source) for x, v in enumerate(row) if v & 0x80]
    if not hair:
        return [row[:] for row in base]
    sx, sy = [p[0] for p in hair], [p[1] for p in hair]
    left, top, right, bottom = bounds
    # Measure the donor hair and host hair silhouette separately for this
    # exact pose. Independent axes matter: profiles are narrower than front
    # views and attacks/falls change the visible hair height.
    scale_x = max(HEAD_SCALE_MIN, min(HEAD_SCALE_MAX,
        (right - left + 1) / (max(sx) - min(sx) + 1)))
    scale_y = max(HEAD_SCALE_MIN, min(HEAD_SCALE_MAX,
        (bottom - top + 1) / (max(sy) - min(sy) + 1)))
    # Eye placement anchors front/profile views; back views use the crown and
    # centre. These anchors are recalculated for every native animation pose.
    if view != 0 and eyes[view] is not None:
        src_eye_x, src_eye_y = eyes[view]
        if mirror:
            src_eye_x = 15 - src_eye_x
        src_anchor = (src_eye_x, src_eye_y)
    else:
        src_anchor = ((min(sx) + max(sx)) / 2, min(sy))
    extra_indices = [0] + [_nearest_palette(rgb, palette) for rgb in extras]
    out = [row[:] for row in base]
    for y, row in enumerate(source):
        for x, encoded in enumerate(row):
            if not encoded & 0x80:
                continue
            mx = 15 - x if mirror else x
            tx = int(round(target_anchor[0] + (mx - src_anchor[0]) * scale_x))
            ty = int(round(target_anchor[1] + (y - src_anchor[1]) * scale_y))
            if not (0 <= tx < SIZE and 0 <= ty < SIZE):
                continue
            role = encoded & 0x7F
            if 1 <= role <= 7 or role == 15:
                colour = role
            elif 16 <= role < 16 + len(extras):
                colour = extra_indices[role - 15]
            else:
                colour = 1
            out[ty][tx] = colour
    return out


def compile_style(gfx, frames, hero, style, head=None):
    entries, ambiguous, preview = {}, set(), []
    palette = [tuple(c) for c in gfx.palette(hero)]
    for pieces, native, bald_image in frames:
        pose = target_head(native)
        if pose is None:
            continue
        styled = bald_image if head is None else overlay_head(bald_image, native, head, palette)
        preview.append((native, styled))
        before, after = cut(gfx, pieces, native, pose), cut(gfx, pieces, styled, pose)
        for key, (data, _) in after.items():
            if data == before[key][0]:
                continue
            pair = (style,) + key
            if pair in ambiguous:
                continue
            if pair in entries and entries[pair] != data:
                ambiguous.add(pair)
                del entries[pair]
                continue
            entries[pair] = data
    return entries, len(ambiguous), preview


def build(gfx, hero="boy", head=None, head_index=0):
    frames = native_frames(gfx, hero)
    entries, conflicts, preview = compile_style(gfx, frames, hero, 0, None)
    if head is not None:
        styled, clashes, preview = compile_style(gfx, frames, hero, head_index + 1, head)
        entries.update(styled)
        conflicts += clashes
    return entries, conflicts, preview


def build_all(gfx, hero="boy"):
    frames = native_frames(gfx, hero)
    all_entries, conflicts, preview = compile_style(gfx, frames, hero, 0, None)
    heads = HP.build(gfx)
    for i, head in enumerate(heads):
        entries, clashes, _ = compile_style(gfx, frames, hero, i + 1, head)
        all_entries.update(entries)
        conflicts += clashes
    return all_entries, conflicts, preview, len(heads)


def pack(entries):
    out = bytearray(b"SOMHAIRF3") + struct.pack("<I", len(entries))
    for style, view, mirror, dx, dy, hf, vf, key in sorted(entries):
        out += struct.pack("<HBBbbBBQ", style, view, mirror, dx, dy, hf, vf, key)
        out += entries[(style, view, mirror, dx, dy, hf, vf, key)]
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
    ap.add_argument("--all-heads", action="store_true",
                    help="compile bald plus every ROM head as a prealigned animation variant")
    ap.add_argument("--head-index", type=int, default=None,
                    help="compile one zero-based ROM head variant, for alignment review")
    a = ap.parse_args()
    gfx = H.HeroGfx(Path(a.rom).read_bytes())
    if a.all_heads:
        if a.sheet:
            ap.error("--sheet and --all-heads are separate; use --head-index to review one style")
        entries, conflicts, frames, heads = build_all(gfx)
        print("compiled %d ROM hairstyles plus bald" % heads, file=sys.stderr)
    elif a.head_index is not None:
        heads = HP.build(gfx)
        if not 0 <= a.head_index < len(heads):
            ap.error("--head-index must be between 0 and %d" % (len(heads) - 1))
        entries, conflicts, frames = build(gfx, head=heads[a.head_index], head_index=a.head_index)
    else:
        entries, conflicts, frames = build(gfx)
    if a.sheet:
        sheet(gfx, frames, a.out)
    else:
        Path(a.out).write_bytes(pack(entries))
    print("%d frames, %d pieces changed, %d ambiguous pieces omitted for native fallback" %
          (len(frames), len(entries), conflicts),
          file=sys.stderr)


if __name__ == "__main__":
    main()

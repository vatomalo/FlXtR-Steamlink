#!/usr/bin/env python3
"""Native NPC objects of Secret of Mana Turbo: decode, bind, plan native walks.

Read-only. Nothing here writes game memory; `walk_command` only formats a
line for the opt-in proxy command (src/som_npc_walk.h), which re-checks every
gate against live WRAM in the frame it applies the walk.

Verified on Turbo (2026-11-13, live WRAM of map 131 and the ROM):
- NPC objects live in actor slots 3..11, 0x200 bytes at $7E:E000 + slot*0x200.
- obj+0x3D kind (>= 0x80 NPC), +0x3E event word, +0x34 offset of the map
  record cache entry ($7E:C800 + 16*n -> record n), +0x02/+0x04 pixel X/Y,
  +0x10 facing, +0x0B layer/floor, +0x44 bit 7 static (native AI off).
- A record at tile (tx, ty) spawns at pixel (16*tx + 8, 16*ty + 4).
- ROM routines the walk relies on: see ROM_FINGERPRINTS.
"""
import hashlib
from collections import deque

from som_memory import WRAM

OBJECT_BASE = 0x7EE000
OBJECT_SIZE = 0x200
NPC_SLOTS = range(3, 12)
CACHE_BASE = 0x7EC800
TALK_TARGET = 0x7E003A
MAP_ID = 0x7E00DC
MAP_WIDTH, MAP_HEIGHT = 0x7E00C0, 0x7E00C2
MAX_WALK_FRAMES = 32
# ACTOR_WALK direction codes (event command 0x32): 0 up, 1 down, 2 right, 3 left.
DIRECTIONS = {"up": (0, (0, -1)), "down": (1, (0, 1)), "right": (2, (1, 0)),
              "left": (3, (-1, 0))}
VELOCITY = {0: (0x00, 0x82, 0x00), 1: (0x00, 0x02, 0x01), 2: (0x02, 0x00, 0x02),
            3: (0x82, 0x00, 0x82)}   # dir -> (obj+6, obj+7, facing obj+0x10)
# Header-stripped HiROM offsets, length, SHA-1 of the Turbo bytes. The first
# two equal the cidwel/som-decomp US sources assembled; the others are Turbo
# relocations that were disassembled (docs/SOM-LIVING-WORLD.md section 2).
ROM_FINGERPRINTS = (
    ("ACTOR_WALK $C1:EEEA", 0x01EEEA, 206, "266cee47bf05e2953a0ab6e6ec001daadb77dad1"),
    ("NPC mover $C0:D229", 0x00D229, 153, "37bd61848d3cc3591eb8cf44729ed22aa097cd38"),
    ("Turbo walk countdown $C0:EB86", 0x00EB86, 48, "55192f7982cd11e59e93e2674d231bb49b58f21f"),
    ("countdown hook $C0:FBEF", 0x00FBEF, 18, "6f361ce7dfce02f4c4aafcacf8b5b1cc39f57522"),
    ("Turbo static-NPC AI gate $F0:78F9", 0x3078F9, 6, "f2756b31b9d5c7a918c6f141cbfc3feea240e9c3"),
    ("AI gate call $C0:F8DF", 0x00F8DF, 10, "d96a5089fd79dd7249a4479ea3bffc2ff3d72391"),
)


def verify_rom(data):
    """[(name, ok)] for every routine fingerprint; copier header skipped."""
    if len(data) % 0x8000 == 512:
        data = data[512:]
    return [(name, hashlib.sha1(data[offset:offset + length]).hexdigest() == digest)
            for name, offset, length, digest in ROM_FINGERPRINTS]


def object_base(slot):
    return OBJECT_BASE + slot * OBJECT_SIZE


def record_pixel(tile_x, tile_y):
    """Spawn pixel of an object record (measured on Turbo map 131)."""
    return 16 * tile_x + 8, 16 * tile_y + 4


def pixel_tile(x, y):
    return x // 16, y // 16


def decode_npcs(wram):
    """Active NPC objects (kind >= 0x80) with the fields the world needs."""
    found = []
    for slot in NPC_SLOTS:
        base = object_base(slot)
        if wram.u8(base) != 1:
            continue
        kind = wram.u8(base + 0x3D)
        if kind < 0x80:
            continue
        cache_offset = wram.u16(base + 0x34)
        x, y = wram.u16(base + 0x02), wram.u16(base + 0x04)
        layer_state = wram.u8(base + 0x0B)
        found.append({
            "slot": slot, "kind": kind, "x": x, "y": y, "tile": pixel_tile(x, y),
            "record": cache_offset // 16 if cache_offset % 16 == 0 else None,
            "event": wram.u16(base + 0x3E) & 0x0FFF,
            "static": bool(wram.u8(base + 0x44) & 0x80),
            "facing": wram.u8(base + 0x10),
            "layer": 2 if layer_state & 0x80 else 1, "floor": layer_state & 3,
            "busy": bool(wram.u8(base + 0x0A) | wram.u8(base + 0x42) | wram.u8(base + 0x1C) |
                         wram.u8(base + 0x60) | wram.u8(base + 0x06) | wram.u8(base + 0x07)),
        })
    return found


def bind(npcs, native):
    """The live object of a persistent NPC: same kind and same map record."""
    for npc in npcs:
        if npc["kind"] == native["kind"] and npc["record"] == native["record"]:
            return npc
    return None


def walk_refusal(wram, slot, kind, x, y, map_id):
    """Python mirror of som_npc_walk_refusal(); None when allowed now."""
    if slot not in NPC_SLOTS:
        return "slot is not an NPC slot"
    if kind < 0x80:
        return "kind is not an NPC kind"
    if wram.u8(0x7EE000) != 1:
        return "no game"
    if wram.u8(0x7E00E4) != 1 or wram.u8(0x7E00E6) != 0x0F:
        return "map transition"
    if wram.u8(0x7E00D0):
        return "event running"
    if wram.u8(0x7E007D) == 1:
        return "ring menu open"
    if wram.u8(0x7E0052) | wram.u8(0x7E00FF) | wram.u8(0x7E00F1):
        return "world paused or map change pending"
    if wram.u8(0x7E00E8) & 0x04:
        return "objects frozen"
    if wram.u8(0x7E00ED) & 0x80:
        return "boss object mode"
    if wram.u16(MAP_ID) & 0x1FF != map_id:
        return "different map"
    base = object_base(slot)
    if wram.u8(base) != 1:
        return "slot not active"
    if wram.u8(base + 0x3D) != kind:
        return "kind changed"
    if not wram.u8(base + 0x44) & 0x80:
        return "NPC has native AI (not static)"
    if wram.u16(base + 2) != x or wram.u16(base + 4) != y:
        return "stale position"
    if wram.u16(TALK_TARGET) == base & 0xFFFF:
        return "NPC is the talk target"
    if any(wram.u8(base + field) for field in (0x0A, 0x42, 0x1C, 0x60, 0x06, 0x07)):
        return "NPC busy"
    return None


def actor_walk_writes(slot, direction, frames, flags71):
    """[(address, value)] the NPC branch of ACTOR_WALK writes (C mirror)."""
    if not 1 <= frames <= MAX_WALK_FRAMES or direction not in VELOCITY:
        raise ValueError("bad walk")
    base = object_base(slot)
    vx, vy, facing = VELOCITY[direction]
    pose = 1
    if flags71 & 0x60:
        if flags71 & 0x40:
            facing = 0
        pose = 0
    return [(base + 0x1C, 0), (base + 0x13, 0), (base + 0x0A, frames), (base + 0x67, 0),
            (base + 0x42, 1), (base + 0x06, vx), (base + 0x07, vy), (base + 0x10, facing),
            (base + 0x30, 0xFF), (base + 0x11, pose)]


def walk_command(map_id, npc, direction, frames):
    """The proxy command line; all fields are integers (validated here too)."""
    code = DIRECTIONS[direction][0]
    if not 1 <= frames <= MAX_WALK_FRAMES:
        raise ValueError("frames out of range")
    values = (map_id, npc["slot"], npc["kind"], npc["x"], npc["y"], code, frames)
    if not all(isinstance(value, int) and value >= 0 for value in values):
        raise ValueError("walk fields must be non-negative integers")
    return "npc_walk " + " ".join(str(value) for value in values)


def frames_for_tiles(tiles):
    """ACTOR_WALK moves 2 px/frame (NPCs can slow to 1 px/frame): 8 frames per
    tile is a lower bound of one tile, so walks undershoot rather than overshoot
    and the closed loop re-plans from the observed position."""
    return max(1, min(MAX_WALK_FRAMES, 8 * tiles))


def walkable_grid(wram, layer, floor, bounds):
    """{(x, y): open?} over bounds (x0, y0, x1, y1) for an NPC on floor.

    Uses the hero rule of som_memory.classify_tile; trigger tiles (doors,
    exits: id >= 0xBD) are never entered, conditional tiles are closed.
    """
    width = max(1, wram.u16(MAP_WIDTH) // 16)
    height = max(1, wram.u16(MAP_HEIGHT) // 16)
    x0, y0, x1, y1 = bounds
    grid = {}
    for y in range(max(0, y0), min(height, y1 + 1)):
        for x in range(max(0, x0), min(width, x1 + 1)):
            tile_id, attributes = wram.tile(x, y, layer)
            kind, _ = WRAM.classify_tile(attributes, floor)
            grid[(x, y)] = kind == "open" and tile_id < 0xBD and (attributes[0] & 3) == floor
    return grid


def plan_route(grid, start, goal, occupied=frozenset(), blocked_edges=frozenset()):
    """Shortest 4-way tile route as a list of direction names, or None."""
    if start == goal:
        return []
    queue, parents = deque([start]), {start: None}
    while queue:
        cell = queue.popleft()
        for name, (_, (dx, dy)) in DIRECTIONS.items():
            nxt = (cell[0] + dx, cell[1] + dy)
            if nxt in parents or not grid.get(nxt) or (cell, name) in blocked_edges:
                continue
            if nxt in occupied and nxt != goal:
                continue
            parents[nxt] = (cell, name)
            if nxt == goal:
                steps = []
                while parents[nxt] is not None:
                    nxt, step = parents[nxt]
                    steps.append(step)
                return steps[::-1]
            queue.append(nxt)
    return None


def first_run(route, limit=4):
    """(direction, tiles) of the straight start of a route."""
    if not route:
        return None
    run = 1
    while run < min(limit, len(route)) and route[run] == route[0]:
        run += 1
    return route[0], run

#!/usr/bin/env python3
"""Copy a Secret of Mana map from the running game as a reference for building
our own (player's request, 2026-11-20: the goblin town, map 133).

The game keeps the whole loaded map in work RAM while the hero is on it
(cidwel/som-decomp docs/maps.md: tile ids $7F:0000 layer 1 and $7F:4000
layer 2, 128 tiles per row; 4 attribute bytes per tile id at $7F:B800 and
$7F:BC00). This tool waits until the hero stands on the map in field play,
then saves, read-only:

    wram.bin     the full 128 KiB work RAM (everything the game holds)
    vram.bin     video RAM (the map's tile graphics), with --vram
    map.json     size, both layers' tile ids and passability, the attributes
                 of every tile id used, exits (ROM table) and object records
                 (monsters, NPCs, chests: som_map_records)
    preview.png  the layout, one coloured 4x4 px cell per tile per layer

Output goes to ~/.local/share/flxtr/maps/map<N>/ (copied game data never
goes into the repository).

    python3 tools/som_map_capture.py --map 133            # wait for map 133
    python3 tools/som_map_capture.py                      # the current map
    python3 tools/som_map_capture.py --map 133 --vram     # graphics too
"""
import argparse
import json
import struct
import subprocess
import sys
import time
import zlib
from pathlib import Path

from som_memory import TILE_SIZE, WRAM

DEFAULT_OUT = Path.home() / ".local/share/flxtr/maps"
EXIT_CACHE = Path.home() / ".local/state/flxtr/som-rom-exits.json"
VRAM_FILE = "/tmp/flxtr-som-vram.bin"
POLL_S = 2.0
STABLE_READS = 2                 # the same map in field play this many reads
CELL = 4                         # preview px per tile
COLORS = {"open": (96, 168, 96), "blocked": (40, 40, 48), "conditional": (200, 168, 64),
          "h1": (120, 150, 200), "h2": (170, 190, 230), "pass": (150, 200, 150),
          "exit": (220, 60, 60), "object": (250, 230, 90)}


def map_id(wram):
    return wram.u16(0x7E00DC) & 0x1FF


def field(wram):
    return wram.u8(0x7E00E4) == 1 and wram.u8(0x7E00E6) == 0x0F and wram.u8(0x7E00D0) == 0


def describe(wram, tables=None, objects=()):
    """map.json content from a field-mode WRAM image."""
    width, height = wram.u16(0x7E00C0) // TILE_SIZE, wram.u16(0x7E00C2) // TILE_SIZE
    here = map_id(wram)
    layers, exits = {}, []
    for layer in (1, 2):
        ids, kinds, attributes = [], [], {}
        for y in range(height):
            row_ids, row_kinds = [], []
            for x in range(width):
                tile_id, attrs = wram.tile(x, y, layer)
                kind, _ = WRAM.classify_tile(attrs, 2)
                terrain = WRAM.terrain_code(attrs) if hasattr(WRAM, "terrain_code") else None
                kind = {"1": "h1", "2": "h2", "=": "pass"}.get(terrain, kind)
                row_ids.append(tile_id)
                row_kinds.append(kind)
                attributes.setdefault(f"{tile_id:02x}", "".join(f"{a:02x}" for a in attrs))
                if tables is not None:
                    exit_ = tables.trigger_exit(here, tile_id)
                    if exit_ is not None:
                        exits.append({"x": x, "y": y, "layer": layer, "to_map": exit_["map_id"],
                                      "arrive": [exit_["x"], exit_["y"]]})
            ids.append(row_ids)
            kinds.append(row_kinds)
        layers[str(layer)] = {"tiles": ids, "kinds": kinds, "attributes": attributes}
    return {"map_id": here, "size": [width, height], "layers": layers, "exits": exits,
            "objects": list(objects), "captured_at": time.strftime("%Y-%m-%d %H:%M:%S"),
            "source": "live WRAM (som_map_capture.py)"}


def png(width, height, pixels):
    """A minimal RGB PNG (no image library needed)."""
    raw = b"".join(b"\x00" + bytes(pixels[y * width * 3:(y + 1) * width * 3])
                   for y in range(height))
    def chunk(kind, data):
        return (struct.pack(">I", len(data)) + kind + data +
                struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF))
    return (b"\x89PNG\r\n\x1a\n" +
            chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def preview(info):
    """Layer 1 left, layer 2 right; exits red, object records yellow."""
    width, height = info["size"]
    gap = 2
    total_w = (2 * width + gap) * CELL
    pixels = bytearray(total_w * height * CELL * 3)
    marks = {(e["layer"], e["x"], e["y"]): "exit" for e in info["exits"]}
    for obj in info["objects"]:
        for layer in (1, 2):
            marks.setdefault((layer, obj["x"], obj["y"]), "object")
    for index, layer in enumerate((1, 2)):
        kinds = info["layers"][str(layer)]["kinds"]
        left = index * (width + gap) * CELL
        for y in range(height):
            for x in range(width):
                color = COLORS.get(marks.get((layer, x, y)) or kinds[y][x], (255, 0, 255))
                for py in range(y * CELL, (y + 1) * CELL):
                    base = (py * total_w + left + x * CELL) * 3
                    pixels[base:base + CELL * 3] = bytes(color) * CELL
    return png(total_w, height * CELL, pixels)


def load_tables():
    try:
        from som_rom import RomTables
        return RomTables.from_json(json.loads(EXIT_CACHE.read_text()))
    except (OSError, ValueError, KeyError, ImportError):
        return None


def load_objects(host, map_id):
    try:
        from som_map_records import load_rom, records
        return records(load_rom(host), map_id)
    except Exception as exc:          # the capture is still useful without them
        print(f"object records unavailable: {exc}", file=sys.stderr)
        return []


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--host", default="root@100.124.74.13")
    parser.add_argument("--map", type=int, help="wait for this map (default: the current one)")
    parser.add_argument("--timeout", type=float, default=3600.0, help="seconds to wait")
    parser.add_argument("--vram", action="store_true", help="also dump video RAM")
    parser.add_argument("--out", type=Path, default=DEFAULT_OUT)
    args = parser.parse_args()
    from som_qwen_bridge import RemoteLink
    link = RemoteLink(args.host)
    try:
        deadline, stable, raw = time.monotonic() + args.timeout, 0, None
        while True:
            try:
                raw = link.fetch_wram(False)
            except (OSError, subprocess.SubprocessError):   # no snapshot yet, or a slow link
                raw = None
            wram = WRAM(raw) if raw is not None else None
            target = args.map if args.map is not None else (map_id(wram) if wram else None)
            if wram is not None and field(wram) and map_id(wram) == target:
                stable += 1
                if stable >= STABLE_READS:
                    break
            else:
                stable = 0
            if time.monotonic() > deadline:
                raise SystemExit(f"map {target} not reached in {args.timeout:.0f} s")
            time.sleep(POLL_S)
        here = map_id(wram)
        folder = args.out / f"map{here}"
        folder.mkdir(parents=True, exist_ok=True)
        (folder / "wram.bin").write_bytes(raw)
        if args.vram:
            link.send_control("vram_dump")
            time.sleep(1.0)
            from som_map_records import remote
            (folder / "vram.bin").write_bytes(remote(args.host, f"cat {VRAM_FILE}", binary=True))
    finally:
        link.close()
    info = describe(wram, load_tables(), load_objects(args.host, here))
    (folder / "map.json").write_text(json.dumps(info))
    (folder / "preview.png").write_bytes(preview(info))
    print(f"map {here} ({info['size'][0]} x {info['size'][1]} tiles, {len(info['exits'])} exit "
          f"tiles, {len(info['objects'])} object records) -> {folder}")


if __name__ == "__main__":
    main()

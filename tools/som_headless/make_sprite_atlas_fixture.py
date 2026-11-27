#!/usr/bin/env python3
"""Create original diagnostic layers for the optional real-ROM atlas smoke test."""
import argparse
import json
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from som_compile_atlas import ARCHETYPES, compile_atlas


def write_tile(path, rows, ink):
    pixels = b"".join(bytes.fromhex(ink if pixel == "#" else "ff00ff")
                      for row in rows for pixel in row)
    path.write_bytes(b"P6\n8 8\n255\n" + pixels)


def make_fixture(directory):
    directory.mkdir(parents=True, exist_ok=True)
    write_tile(directory / "mask.ppm", [
        "..####..", ".######.", "##.##.##", "########",
        ".##..##.", "..####..", "...##...", "........",
    ], "eeeeee")
    write_tile(directory / "crest.ppm", [
        "...##...", "..#..#..", "...##...", "........",
        "........", "........", "........", "........",
    ], "eeaa33")
    tile_ranges = (range(0x1C0, 0x1D8), range(0x1C8, 0x1D0), range(0x1E0, 0x1E8))
    poses = ((474, "down"), (480, "down"), (488, "down"), (496, "up"), (504, "right"))
    for archetype, tiles in zip(ARCHETYPES, tile_ranges):
        pieces = [{"tile": tile, "layers": [
            {"source": "mask", "rect": [0, 0, 8, 8]},
            {"source": "crest", "rect": [0, 0, 8, 8], "socket": "head"},
        ]} for tile in tiles]
        manifest = directory / (archetype + ".json")
        manifest.write_text(json.dumps({
            "asset_id": "diagnostic-mask-and-crest",
            "version": 1, "archetype": archetype,
            "sources": {
                "mask": {"path": "mask.ppm", "palette": {"eeeeee": 8}},
                "crest": {"path": "crest.ppm", "palette": {"eeaa33": 12}},
            },
            "frames": [{"pose": pose, "action": "walk", "facing": facing, "duration": 1,
                        "sockets": {"head": [0, 0]}, "tiles": pieces}
                       for pose, facing in poses],
        }))
        yield compile_atlas(manifest, directory / "compiled")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    for slot, path in enumerate(make_fixture(parser.parse_args().directory.resolve()), 1):
        print(f"FLXTR_SOM_ATLAS_P{slot}={path}")

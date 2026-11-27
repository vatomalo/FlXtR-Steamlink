#!/usr/bin/env python3
"""Compile original or user-supplied sprite layers to a native OBJ tile bank."""
import argparse
import hashlib
import io
import json
import os
from pathlib import Path
import struct
import tempfile

ARCHETYPES = {"randi": 0, "primm": 1, "popoi": 2}
ACTIONS = frozenset("""idle walk run turn attack charged cast hurt knockdown recovery
death scripted sit sleep combo guard parry evade ukemi somersault cinematic
discipline weapon_flow teleport""".split())
FACING = {"up", "down", "left", "right"}
MAX_RECORDS = 16384
COMPILER_VERSION = b"som-atlas-compiler-v1\0"


def image(data):
    """PPM fixtures work without dependencies. Pillow supplies PNG decoding."""
    if data.startswith(b"P6\n"):
        magic, size, maximum, pixels = data.split(b"\n", 3)
        width, height = map(int, size.split())
        if not 0 < width <= 4096 or not 0 < height <= 4096:
            raise ValueError("invalid PPM dimensions")
        if maximum != b"255" or len(pixels) != 3 * width * height:
            raise ValueError("invalid PPM pixel data")
        return width, height, ((*rgb, 255) for rgb in zip(*[iter(pixels)] * 3))
    try:
        from PIL import Image
    except ImportError as exc:
        raise ValueError("install Pillow to import PNG sprite assets") from exc
    with Image.open(io.BytesIO(data)) as source:
        if source.format != "PNG" or max(source.size) > 4096:
            raise ValueError("expected a PNG no larger than 4096 pixels per axis")
        rgba = source.convert("RGBA")
        return *rgba.size, rgba.getdata()


def indexed(data, colours, key):
    width, height, pixels = image(data)
    result = bytearray()
    for pixel in pixels:
        rgb, alpha = pixel[:3], pixel[3]
        if not alpha or rgb == key:
            result.append(0)
        elif alpha != 255:
            raise ValueError("sprite pixels must have binary alpha")
        elif rgb not in colours:
            raise ValueError(f"unmapped opaque RGB colour {rgb}")
        else:
            result.append(colours[rgb])
    return width, height, result


def pair(value):
    x, y = value
    if type(x) is not int or type(y) is not int:
        raise ValueError("coordinates must be integers")
    return x, y


def compile_frames(manifest, sources):
    key_hex = manifest.get("chroma_key", "ff00ff").removeprefix("#")
    key = tuple(bytes.fromhex(key_hex))
    if key not in ((255, 0, 255), (0, 255, 0)):
        raise ValueError("chroma key must be pure magenta or pure green")
    slot = ARCHETYPES[manifest["archetype"]]
    assets = {}
    for name, asset in manifest["sources"].items():
        palette = {tuple(bytes.fromhex(rgb)): idx for rgb, idx in asset["palette"].items()}
        if any(len(rgb) != 3 or type(idx) is not int or not 1 <= idx <= 15
               for rgb, idx in palette.items()):
            raise ValueError(f"{name}: opaque colours must map to OBJ indices 1–15")
        compatible = asset.get("compatible", list(ARCHETYPES))
        if manifest["archetype"] not in compatible:
            raise ValueError(f"{name}: incompatible with {manifest['archetype']}")
        assets[name] = indexed(sources[name], palette, key)

    records = {}
    covered = set()
    for frame in manifest["frames"]:
        action, facing = frame["action"], frame["facing"]
        if action not in ACTIONS or facing not in FACING:
            raise ValueError(f"unknown canonical action or facing: {action}/{facing}")
        if type(frame["duration"]) is not int or frame["duration"] < 1:
            raise ValueError("frame duration must be positive")
        pose = frame["pose"]
        if type(pose) is not int or not 0 <= pose <= 65535:
            raise ValueError("native pose must fit in 16 bits")
        sockets = frame.get("sockets", {})
        for socket in sockets.values():
            pair(socket)
        for piece in frame["tiles"]:
            tile = piece["tile"]
            if type(tile) is not int or not 0 <= tile < 512 or (pose, tile) in records:
                raise ValueError(f"invalid or duplicate tile mapping for pose {pose:#06x}")
            canvas = bytearray(64)
            origin_x, origin_y = pair(piece.get("origin", [0, 0]))
            for layer in piece["layers"]:  # back to front
                width, height, pixels = assets[layer["source"]]
                x, y, w, h = layer["rect"]
                if any(type(v) is not int for v in (x, y, w, h)) or not (
                        0 <= x < x+w <= width and 0 <= y < y+h <= height):
                    raise ValueError("layer rectangle lies outside its source")
                dx, dy = pair(layer.get("offset", [0, 0]))
                if "socket" in layer:
                    sx, sy = pair(sockets[layer["socket"]])
                    dx += sx - origin_x
                    dy += sy - origin_y
                for py in range(max(0, -dy), min(h, 8 - dy)):
                    for px in range(max(0, -dx), min(w, 8 - dx)):
                        ink = pixels[(y+py)*width+x+px]
                        if ink:
                            canvas[(dy+py)*8+dx+px] = ink
            records[pose, tile] = canvas
        if not frame["tiles"]:
            raise ValueError("a mapped frame needs at least one tile")
        covered.add(f"{action}/{facing}")
    missing = set(manifest.get("required", [])) - covered
    if missing:
        raise ValueError("missing animation coverage: " + ", ".join(sorted(missing)))
    if not 0 < len(records) <= MAX_RECORDS:
        raise ValueError("atlas needs 1–16384 tile records")
    return struct.pack("<4sBBH", b"SAT1", slot, 0, len(records)) + b"".join(
        struct.pack("<HH", *ids) + records[ids] for ids in sorted(records))


def compile_atlas(path, output):
    manifest = json.loads(path.read_text(encoding="utf-8"))
    if manifest["version"] != 1 or not manifest["asset_id"]:
        raise ValueError("expected a named version 1 manifest")
    sources = {name: (path.parent / asset["path"]).read_bytes()
               for name, asset in manifest["sources"].items()}
    digest = hashlib.sha256(COMPILER_VERSION)
    digest.update(json.dumps(manifest, sort_keys=True, separators=(",", ":")).encode())
    for name in sorted(sources):
        digest.update(hashlib.sha256(sources[name]).digest())
    destination = output / (digest.hexdigest() + ".sat")
    if destination.is_file():
        return destination
    data = compile_frames(manifest, sources)
    output.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(dir=output, delete=False) as stream:
        temporary = Path(stream.name)
        try:
            stream.write(data)
            stream.close()
            os.replace(temporary, destination)
        finally:
            temporary.unlink(missing_ok=True)
    return destination


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("manifest", type=Path)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    try:
        print(compile_atlas(args.manifest, args.out))
    except (TypeError, ValueError, OSError, KeyError) as error:
        parser.exit(2, f"sprite atlas: {error}\n")


if __name__ == "__main__":
    main()

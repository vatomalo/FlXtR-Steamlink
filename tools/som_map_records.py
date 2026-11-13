#!/usr/bin/env python3
"""List a map's object records and turn a monster record into a Rabite.

The game creates monsters from per-map 8-byte object records when a map
loads (cidwel/som-decomp docs/maps.md section 10: pointer table at $C8:7000,
record +5 = object id, 0x00 = Rabite, 0x00-0x56 monsters, 0x57-0x7F bosses,
0x80+ NPCs/chests). A monster created that way is complete: drawn, animated,
with AI, death, EXP and drops. This tool never edits the ROM file: it writes
a patch line that som_ai_proxy applies to its in-memory ROM copy at load.

    python3 tools/som_map_records.py list               # records of the current map
    python3 tools/som_map_records.py rabite 2           # record 2 -> Rabite
    python3 tools/som_map_records.py patches            # show active patches
    python3 tools/som_map_records.py clear              # remove all patches

After `rabite` or `clear`, quit and relaunch Turbo (patches apply at game
load), then walk into the map: the record spawns as a Rabite. Each patch line
names the byte it expects, so a different ROM is never altered.
"""
import argparse
import subprocess
import sys

from som_memory import WRAM
from som_qwen_bridge import SSH_BASE, RemoteLink

ROM_PATH = "/home/apps/retroarch/roms/Snes/Secret of Mana, Turbo.smc"
PATCH_FILE = "/home/apps/retroarch/cores/experimental/som_ai_rom_patch.txt"
TABLE = 0x087000          # $C8:7000 in a HiROM image without copier header
BANK_C8 = 0x080000
RABITE = 0x00


def remote(host, command, binary=False):
    result = subprocess.run([*SSH_BASE, host, command], capture_output=True,
                            check=True, timeout=60)
    return result.stdout if binary else result.stdout.decode()


def load_rom(host):
    data = remote(host, f'cat "{ROM_PATH}"', binary=True)
    header = 512 if len(data) % 0x8000 == 512 else 0
    rom = data[header:]
    if len(rom) < TABLE + 0x400:
        raise SystemExit(f"ROM too small ({len(rom)} bytes)")
    pointers = [rom[TABLE + 2 * m] | rom[TABLE + 2 * m + 1] << 8 for m in range(400)]
    if any(b < a for a, b in zip(pointers, pointers[1:])) or min(pointers) < 0x7000:
        raise SystemExit("the map pointer table at $C8:7000 does not look like Secret of "
                         "Mana's; the Turbo layout differs, nothing changed")
    return rom


def records(rom, map_id):
    pointer = rom[TABLE + 2 * map_id] | rom[TABLE + 2 * map_id + 1] << 8
    following = rom[TABLE + 2 * map_id + 2] | rom[TABLE + 2 * map_id + 3] << 8
    found = []
    if following <= pointer + 8:
        return found
    for index in range((following - pointer - 8) // 8):
        offset = BANK_C8 + pointer + 8 + 8 * index
        record = rom[offset:offset + 8]
        object_id = record[5]
        kind = ("monster" if object_id < 0x57 else "boss" if object_id < 0x80
                else "npc/chest/switch")
        found.append({"index": index, "offset": offset, "id": object_id, "kind": kind,
                      "x": record[2] & 0x7F, "y": record[3],
                      "flag": record[0], "range": (record[1] >> 4, record[1] & 0x0F)})
    return found


def current_map(host):
    link = RemoteLink(host)
    try:
        raw = link.fetch_wram(False)
    finally:
        link.close()
    if raw is None:
        raise SystemExit("no WRAM snapshot; is Turbo running with som_ai_proxy?")
    return WRAM(raw).u16(0x7E00DC) & 0x1FF


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("command", choices=("list", "rabite", "patches", "clear"))
    parser.add_argument("record", nargs="?", type=int)
    parser.add_argument("--map", type=int, help="map id (default: the current map)")
    parser.add_argument("--host", default="root@100.124.74.13")
    parser.add_argument("--allow-npc", action="store_true",
                        help="also allow replacing NPC/chest records (may break events)")
    args = parser.parse_args()

    if args.command == "patches":
        print(remote(args.host, f"cat {PATCH_FILE} 2>/dev/null || true") or "no patches")
        return 0
    if args.command == "clear":
        remote(args.host, f"rm -f {PATCH_FILE}")
        print("Patches removed. Relaunch Turbo to load the original records.")
        return 0

    map_id = args.map if args.map is not None else current_map(args.host)
    rom = load_rom(args.host)
    found = records(rom, map_id)
    if args.command == "list":
        print(f"map {map_id}: {len(found)} object record(s)")
        for record in found:
            name = "Rabite" if record["id"] == RABITE else f"id {record['id']:#04x}"
            print(f"  [{record['index']}] {record['kind']:16} {name:9} tile "
                  f"({record['x']},{record['y']}) flag {record['flag']:#04x} "
                  f"range {record['range'][0]}-{record['range'][1]}")
        if not any(record["kind"] == "monster" for record in found):
            print("  no monster record here; try a map with monsters")
        return 0

    if args.record is None or not 0 <= args.record < len(found):
        raise SystemExit(f"choose a record index 0..{len(found) - 1} (see `list`)")
    record = found[args.record]
    if record["kind"] != "monster" and not args.allow_npc:
        raise SystemExit(f"record {args.record} is a {record['kind']}; pick a monster "
                         "record (or --allow-npc, which can break events)")
    if record["id"] == RABITE:
        raise SystemExit("that record already is a Rabite")
    offset, expected = record["offset"] + 5, record["id"]
    remote(args.host, f"printf 'patch %06x %02x %02x\\n' {offset} {expected} {RABITE} "
                      f">> {PATCH_FILE}")
    print(f"Map {map_id} record {args.record} (id {expected:#04x} at tile "
          f"({record['x']},{record['y']})) will spawn as a Rabite.")
    print("Quit and relaunch Turbo, then enter that map. Undo: "
          "python3 tools/som_map_records.py clear")
    return 0


if __name__ == "__main__":
    sys.exit(main())

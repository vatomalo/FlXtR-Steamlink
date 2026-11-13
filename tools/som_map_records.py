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
import json
import sqlite3
import subprocess
import sys
from pathlib import Path

from som_memory import WRAM
from som_qwen_bridge import SSH_BASE, RemoteLink

ROM_PATH = "/home/apps/retroarch/roms/Snes/Secret of Mana, Turbo.smc"
PATCH_FILE = "/home/apps/retroarch/cores/experimental/som_ai_rom_patch.txt"
TABLE = 0x087000          # $C8:7000 in a HiROM image without copier header
BANK_C8 = 0x080000
RABITE = 0x00
# Ordinary monsters; 0x57-0x7F are bosses, 0x80+ NPCs/chests/switches.
LAST_MONSTER = 0x56
DEFAULT_HISTORY = Path.home() / ".local/state/flxtr/som-ai.sqlite"
# Turbo relocates a few headers (map 182's lies at $C8:0D00); tolerate that
# many descending pointers and refuse only the maps they affect.
MAX_RELOCATED = 16
MAX_RECORDS = 64


class RecordError(ValueError):
    pass


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
    descending = sum(b < a for a, b in zip(pointers, pointers[1:]))
    if descending > MAX_RELOCATED:
        raise SystemExit(f"the map pointer table at $C8:7000 does not look like Secret of "
                         f"Mana's ({descending} pointers descend); nothing changed")
    return rom


def records(rom, map_id):
    """Object records of map_id; RecordError when its bounds are unreliable."""
    pointer = rom[TABLE + 2 * map_id] | rom[TABLE + 2 * map_id + 1] << 8
    following = rom[TABLE + 2 * map_id + 2] | rom[TABLE + 2 * map_id + 3] << 8
    previous = (rom[TABLE + 2 * map_id - 2] | rom[TABLE + 2 * map_id - 1] << 8
                if map_id else pointer)
    found = []
    if following == pointer or (following == pointer + 8 and previous <= pointer):
        return found
    if (following < pointer or previous > pointer or
            (following - pointer - 8) % 8 or (following - pointer - 8) // 8 > MAX_RECORDS):
        raise RecordError(f"map {map_id}: record table relocated in this ROM "
                          f"(pointer {pointer:#06x}, next {following:#06x}); refusing")
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


def live_wram(host):
    link = RemoteLink(host)
    try:
        raw = link.fetch_wram(False)
    finally:
        link.close()
    if raw is None:
        raise SystemExit("no WRAM snapshot; is Turbo running with som_ai_proxy?")
    return WRAM(raw)


def current_map(host):
    return live_wram(host).u16(0x7E00DC) & 0x1FF


def seen_identities(map_id, wram=None, here=None, history_db=DEFAULT_HISTORY):
    """Monster identities (+0x180) seen on map_id: live WRAM if the hero is
    there, plus every identity the AI's observation log recorded there."""
    seen = set()
    if wram is not None and here == map_id:
        seen |= {enemy["identity"] for enemy in (wram.enemy(i) for i in range(3))
                 if enemy["active"]}
    try:
        connection = sqlite3.connect(f"file:{history_db}?mode=ro", uri=True)
        try:
            for (encoded,) in connection.execute("SELECT state FROM observations"):
                state = json.loads(encoded)
                if (state.get("tile_context") or {}).get("map_id") != map_id:
                    continue
                seen |= {enemy.get("identity") for enemy in state.get("enemy_slots", [])
                         if enemy.get("active")}
        finally:
            connection.close()
    except sqlite3.Error:
        pass
    return seen


def cross_check(found, seen):
    """(matched, extra): seen identities that are / are not record ids.

    Turbo is a ROM hack: a map's records are trusted for patching only when
    a monster the game really spawned there carries one of their ids. Turbo
    also spawns monsters that are in no record (map 129's 0x0a, map 128),
    which are reported, not held against the table.
    """
    ids = {record["id"] for record in found if record["kind"] == "monster"}
    return seen & ids, seen - ids


def tile_near_hero(wram, distance=2):
    """An open tile `distance` steps from the hero (verified grid), or None.

    Two tiles leave room to turn and swing; straight lines are preferred.
    """
    context = wram.tile_context(radius=3)
    if not context.get("runtime_verified"):
        return None
    rows, radius = context["passability_rows"], context["radius"]
    hero_x, hero_y = context["actor_tile"]
    width, height = context["map_size_tiles"]
    choices = []
    for dy in range(-radius, radius + 1):
        for dx in range(-radius, radius + 1):
            if abs(dx) + abs(dy) != distance or rows[radius + dy][radius + dx] != ".":
                continue
            choices.append((0 if dx == 0 or dy == 0 else 1, dy, dx))
    if not choices:
        return None
    _, dy, dx = min(choices)
    return (hero_x + dx) % width, (hero_y + dy) % height


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("command", choices=("list", "spawn", "rabite", "patches", "clear"))
    parser.add_argument("record", nargs="?", type=int)
    parser.add_argument("monster", nargs="?", type=lambda text: int(text, 0),
                        help="spawn: ordinary monster id 0x00-0x56 (default Rabite)")
    parser.add_argument("--map", type=int, help="map id (default: the current map)")
    parser.add_argument("--host", default="root@100.124.74.13")
    parser.add_argument("--near-hero", action="store_true",
                        help="spawn: also move the record's spawn tile next to the hero")
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

    wram = live_wram(args.host)
    here = wram.u16(0x7E00DC) & 0x1FF
    map_id = args.map if args.map is not None else here
    rom = load_rom(args.host)
    try:
        found = records(rom, map_id)
    except RecordError as exc:
        raise SystemExit(str(exc))
    matched, extra = cross_check(found, seen_identities(map_id, wram, here))
    if args.command == "list":
        print(f"map {map_id}: {len(found)} object record(s)")
        for record in found:
            name = "Rabite" if record["id"] == RABITE else f"id {record['id']:#04x}"
            print(f"  [{record['index']}] {record['kind']:16} {name:9} tile "
                  f"({record['x']},{record['y']}) flag {record['flag']:#04x} "
                  f"range {record['range'][0]}-{record['range'][1]}")
        if not any(record["kind"] == "monster" for record in found):
            print("  no monster record here; try a map with monsters")
        print(f"  monsters seen here matching records: {sorted(matched) or 'none'}; "
              f"spawned outside these records: {sorted(extra) or 'none'}")
        return 0

    if not matched:
        raise SystemExit(f"no monster seen on map {map_id} matches its records "
                         f"(seen only {sorted(extra) or 'nothing'}); visit it with the AI "
                         "first (Turbo layout check)")
    if args.record is None or not 0 <= args.record < len(found):
        raise SystemExit(f"choose a record index 0..{len(found) - 1} (see `list`)")
    record = found[args.record]
    if record["kind"] != "monster" and not args.allow_npc:
        raise SystemExit(f"record {args.record} is a {record['kind']}; pick a monster "
                         "record (or --allow-npc, which can break events)")
    monster = RABITE if args.command == "rabite" or args.monster is None else args.monster
    if not 0 <= monster <= LAST_MONSTER:
        raise SystemExit(f"{monster:#04x} is not an ordinary monster id (0x00-0x56)")
    lines = []
    if record["id"] != monster:
        lines.append((record["offset"] + 5, record["id"], monster))
    where = f"tile ({record['x']},{record['y']})"
    if args.near_hero:
        if map_id != here:
            raise SystemExit("--near-hero needs the hero standing on that map")
        tile = tile_near_hero(wram)
        if tile is None:
            raise SystemExit("no verified open tile two steps from the hero; move a bit")
        old_x, old_y = rom[record["offset"] + 2], rom[record["offset"] + 3]
        new_x, new_y = (old_x & 0x80) | tile[0], (old_y & 0x80) | tile[1]
        for offset, old, new in ((record["offset"] + 2, old_x, new_x),
                                 (record["offset"] + 3, old_y, new_y)):
            if old != new:
                lines.append((offset, old, new))
        where = f"tile ({tile[0]},{tile[1]}) next to the hero"
    if not lines:
        raise SystemExit(f"that record already is id {monster:#04x} at that tile")
    for offset, expected, value in lines:
        remote(args.host, f"printf 'patch %06x %02x %02x\\n' {offset} {expected} {value} "
                          f">> {PATCH_FILE}")
    print(f"Map {map_id} record {args.record} (id {record['id']:#04x}) will spawn as "
          f"monster {monster:#04x} at {where}.")
    print("Quit and relaunch Turbo, then enter that map. Undo: "
          "python3 tools/som_map_records.py clear")
    return 0


if __name__ == "__main__":
    sys.exit(main())

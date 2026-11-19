"""Read-only Secret of Mana map exit tables from the ROM image.

Formats follow the public documentation in cidwel/som-decomp `docs/maps.md`
sections 2 and 8 (USA ROM, HiROM, bank $C8 = file offset 0x080000):

- `$C8:7000 + 2*m`  map header pointer (8 header bytes, then object records)
- `$C8:4000 + 2*m`  event list pointer (words up to the next map's pointer)
- `$C8:3000 + 4*n`  transition n: word0 bits 0-8 target map, bits 9-15 start
  tile X; byte 2 bit 0 = start on layer 2, bits 1-7 start tile Y; byte 3 flags.
- A hero standing on tile id t >= 0xC0 - (header[3] & 3) runs event-list word
  (t - base) + (header[3] bit 2). Words 0x800-0xBFF are plain transitions
  (n = word - 0x800); lower words are scripts (dialogs, story gates, scripted
  map changes) and are not followed here.

Turbo is a ROM hack, so nothing here is trusted on its own: the tables are
only sanity-checked. ObservationHistory cross-checks predicted exits against
map crossings the hero really makes before the planner may use them.
"""
import hashlib
import json
from pathlib import Path

BANK_C8 = 0x080000
MAP_POINTERS = 0x7000
EVENT_POINTERS = 0x4000
TRANSITIONS = 0x3000
MAP_COUNT = 0x200
TRANSITION_COUNT = 0x400
TRIGGER_FIRST = 0xC0
# Painted exits go as low as 0xBD (header byte 3 bits 0-1 lower the base).
TRIGGER_LOWEST = 0xBD
# Descending table pointers tolerated (Turbo relocations) and the longest
# event list believed: trigger ids run 0xBD..0xFF.
MAX_RELOCATED = 16
MAX_EVENTS = 0x44


class RomLayoutError(ValueError):
    """The image does not have Secret of Mana's map tables where expected."""


def strip_header(data):
    """Drop a 512-byte copier header if present."""
    return data[512:] if len(data) % 0x8000 == 512 else data


class RomTables:
    """Parsed exit tables; built from a ROM image or a cached JSON dict."""

    def __init__(self, maps, transitions, digest=None, scripted=None):
        # maps: {map_id: {"base": int, "entry": bool, "events": [int, ...]}}
        self.maps = maps
        self.transitions = transitions   # [(map, x, y, layer2, flags), ...]
        self.digest = digest
        # Map changes inside event scripts (scan_scripted_exits): {map:
        # [(target, (flag, lo, hi) or None)]}. None: not scanned (old cache).
        self.scripted = scripted

    @classmethod
    def from_rom(cls, data):
        # The digest covers the file exactly as stored, so a remote sha1sum
        # of the ROM file can be compared with the cache without fetching it.
        digest = hashlib.sha1(bytes(data)).hexdigest()
        rom = strip_header(bytes(data))
        if len(rom) < BANK_C8 + 0x10000:
            raise RomLayoutError(f"ROM too small ({len(rom)} bytes)")

        def word(offset):
            return rom[BANK_C8 + offset] | rom[BANK_C8 + offset + 1] << 8

        map_pointers = [word(MAP_POINTERS + 2 * m) for m in range(MAP_COUNT)]
        event_pointers = [word(EVENT_POINTERS + 2 * m) for m in range(MAP_COUNT)]
        # Turbo relocates a few headers and event lists (seen: map 182's
        # header, the lists of maps 51/133/183/248/250), so the layout is
        # accepted when it is overwhelmingly SoM's, and each map whose list
        # bounds are out of order is left without exits (fail closed).
        for name, pointers in (("map header", map_pointers), ("event list", event_pointers)):
            descending = sum(b < a for a, b in zip(pointers, pointers[1:]))
            if descending > MAX_RELOCATED:
                raise RomLayoutError(f"{descending} {name} pointers descend; not SoM's layout")
        transitions = []
        for n in range(TRANSITION_COUNT):
            offset = BANK_C8 + TRANSITIONS + 4 * n
            b0, b1, b2, b3 = rom[offset:offset + 4]
            transitions.append(((b0 | b1 << 8) & 0x1FF, b1 >> 1, b2 >> 1, b2 & 1, b3))
        maps = {}
        for m in range(MAP_COUNT - 1):
            pointer = map_pointers[m]
            if map_pointers[m + 1] < pointer + 8:
                continue                      # no header of its own
            header = rom[BANK_C8 + pointer:BANK_C8 + pointer + 8]
            if header == b"\xff" * 8:
                continue                      # end marker
            start, end = event_pointers[m], event_pointers[m + 1]
            if not (EVENT_POINTERS + 2 * MAP_COUNT <= start <= end and
                    end - start <= 2 * MAX_EVENTS and
                    event_pointers[m - 1 if m else 0] <= start):
                continue                      # relocated or unbounded list
            events = [word(start + 2 * i) for i in range((end - start) // 2)]
            maps[m] = {"base": TRIGGER_FIRST - (header[3] & 3),
                       "entry": bool(header[3] & 4), "events": events}
        if len(maps) < 100:
            raise RomLayoutError(f"only {len(maps)} map headers found")
        scripted = scan_scripted_exits(rom, maps, transitions, map_pointers)
        return cls(maps, transitions, digest, scripted)

    def to_json(self):
        data = {"digest": self.digest, "transitions": self.transitions,
                "maps": {str(m): info for m, info in self.maps.items()}}
        if self.scripted is not None:
            data["scripted"] = {str(m): [[t, list(c) if c else None] for t, c in exits]
                                for m, exits in self.scripted.items()}
        return data

    @classmethod
    def from_json(cls, data):
        scripted = data.get("scripted")
        if scripted is not None:
            scripted = {int(m): [(t, tuple(c) if c else None) for t, c in exits]
                        for m, exits in scripted.items()}
        return cls({int(m): info for m, info in data["maps"].items()},
                   [tuple(item) for item in data["transitions"]], data.get("digest"),
                   scripted)

    def scripted_into(self, target, flags=None):
        """Maps whose event scripts can change to target: their trigger
        tiles, entry event or NPC talks. With flags (256 bytes of
        $7E:CF00) a gated change counts only while its IF_FLAG_RANGE holds.
        These are never planner exits: where on the map the script starts
        (a tile, an NPC, a story beat) is not known here."""
        found = set()
        for map_id, exits in (self.scripted or {}).items():
            for to_map, condition in exits:
                if to_map != target:
                    continue
                if condition and flags is not None:
                    flag, lo, hi = condition
                    if not lo <= flags[flag] <= hi:
                        continue
                found.add(map_id)
        return found

    def trigger_exit(self, map_id, tile_id):
        """Direct transition started by standing on tile_id, or None.

        The game indexes the event list without a bound (a short list reads
        the next map's words); that is reproduced only within this map's own
        list, anything beyond is reported as unknown.
        """
        info = self.maps.get(map_id)
        if info is None or tile_id < info["base"]:
            return None
        index = tile_id - info["base"] + (1 if info["entry"] else 0)
        if index >= len(info["events"]):
            return None
        event = info["events"][index]
        if not 0x800 <= event < 0xC00:
            return None
        target, x, y, layer2, flags = self.transitions[event - 0x800]
        return {"event": event, "transition": event - 0x800, "map_id": target,
                "x": x, "y": y, "layer": 2 if layer2 else 1, "exit_code": flags & 0x1F}

    def neighbours(self, map_id):
        """Maps directly reachable from map_id through its plain trigger exits."""
        info = self.maps.get(map_id)
        if info is None:
            return set()
        found = set()
        for offset in range(len(info["events"]) - (1 if info["entry"] else 0)):
            exit_ = self.trigger_exit(map_id, info["base"] + offset)
            if exit_ is not None and exit_["map_id"] != map_id:
                found.add(exit_["map_id"])
        return found


def scan_exit_tiles(wram, map_id, tables):
    """Exit tiles of the loaded work map: [{layer, x, y, tile_id, map_id, ...}].

    Reads the live tile buffers of both layers (any trigger id is a marker
    clone or painted exit present under the current story flags).
    """
    width, height = wram.u16(0x7E00C0) // 16, wram.u16(0x7E00C2) // 16
    if not (1 <= width <= 128 and 1 <= height <= 128):
        return []
    info = tables.maps.get(map_id) if tables is not None else None
    if info is None:
        return []
    exits = []
    for layer in (1, 2):
        for y in range(height):
            for x in range(width):
                tile_id, _ = wram.tile(x, y, layer)
                if tile_id < max(info["base"], TRIGGER_LOWEST):
                    continue
                exit_ = tables.trigger_exit(map_id, tile_id)
                if exit_ is None or exit_["map_id"] == map_id:
                    continue
                exits.append({"layer": layer, "x": x, "y": y, "tile_id": tile_id,
                              "to_map": exit_["map_id"],
                              "to_tile": [exit_["x"], exit_["y"]],
                              "to_layer": exit_["layer"]})
    return exits


# Event scripts (cidwel/som-decomp docs/cutscene-engine.md sections 4, 6, 8):
# ids 0x000-0x3FF in bank $C9, 0x400-0x7FF in $CA, each bank starting with a
# 1024-word pointer table; a script ends where the next entry starts.
SCRIPT_BANKS = (0x090000, 0x0A0000)
SCRIPT_MAX = 0x1000                      # bytes walked per script at most
SCRIPT_DEPTH = 64                        # events followed per start at most
TEXT_ARGUMENT = {0x57, 0x59, 0x5A, 0x5C}  # text codes followed by one byte


def _op_length(op, a):
    if op < 0x10 or op == 0x35 or op in (0x3D, 0x3E, 0x3F, 0x44, 0x45, 0x46, 0x47, 0x4F):
        return 1
    if op == 0x2D:
        return 4 if a in (5, 6) or 0x80 <= a <= 0x8F else 2
    if op in (0x30, 0x31, 0x32, 0x33, 0x34, 0x36, 0x37, 0x42, 0x43):
        return 3
    if op in (0x39, 0x3A, 0x48) or 0x49 <= op <= 0x4E:
        return 4
    if op in (0x40, 0x41):
        return 5
    return 2                             # 0x10-0x2F, 0x38, 0x3B, 0x3C


def _script_span(rom, event):
    bank, index = SCRIPT_BANKS[event >> 10], event & 0x3FF
    start = rom[bank + 2 * index] | rom[bank + 2 * index + 1] << 8
    end = (rom[bank + 2 * index + 2] | rom[bank + 2 * index + 3] << 8
           if index < 0x3FF else start + SCRIPT_MAX)
    if not 0x800 <= start <= end or end - start > SCRIPT_MAX:
        return None                      # relocated or unbounded: fail closed
    return bank + start, bank + end


def _script_map_changes(rom, event, seen):
    """[(transition, (flag, lo, hi) or None)] reachable from this event.

    Follows GOTO and CALL; a MAP_CHANGE right after IF_FLAG_RANGE keeps
    that test as its condition (and so does everything a gated GOTO/CALL
    leads to). Other branches are taken as possible.
    """
    if event in seen or event >= 0x800 or len(seen) >= SCRIPT_DEPTH:
        return []
    seen.add(event)
    span = _script_span(rom, event)
    if span is None:
        return []
    pc, end = span
    found, condition = [], None
    while pc < end:
        op = rom[pc]
        if op >= 0x50:                   # text block up to the first command
            while pc < end and rom[pc] >= 0x50:
                if rom[pc] == 0x7D:      # credits segment 0x7D ... 0x7E
                    while pc < end and rom[pc] != 0x7E:
                        pc += 1
                pc += 2 if rom[pc] in TEXT_ARGUMENT else 1
            continue
        a = rom[pc + 1] if pc + 1 < len(rom) else 0
        guard, condition = condition, None
        if op == 0x42:
            condition = (a, rom[pc + 2] >> 4, rom[pc + 2] & 15)
        elif 0x18 <= op <= 0x1B:
            found.append(((op & 3) * 256 + a, guard))
        elif 0x10 <= op <= 0x17 or 0x20 <= op <= 0x27:
            found += [(t, c or guard) for t, c in
                      _script_map_changes(rom, (op & 7) * 256 + a, seen)]
            if op <= 0x17 and guard is None:
                break                    # an unconditional GOTO never returns
        elif op == 0x00 and guard is None:
            break
        pc += _op_length(op, a)
    return found


def scan_scripted_exits(rom, maps, transitions, map_pointers):
    """{map: [(target map, condition)]} from the event scripts a map starts:
    its entry event, its trigger tiles' script words and its NPC records'
    talk events (+6/+7 of each 8-byte object record)."""
    exits = {}
    for map_id, info in maps.items():
        events = [e & 0xFFF for e in info["events"]]
        start = map_pointers[map_id]
        end = map_pointers[map_id + 1] if map_id + 1 < len(map_pointers) else start
        for record in range(BANK_C8 + start + 8, BANK_C8 + end - 7, 8):
            events.append((rom[record + 6] | rom[record + 7] << 8) & 0xFFF)
        found = set()
        for event in events:
            if event >= 0x800:
                continue
            for transition, condition in _script_map_changes(rom, event, set()):
                target = transitions[transition][0]
                if target != map_id:
                    found.add((target, condition))
        if found:
            exits[map_id] = sorted(found, key=lambda item: (item[0], item[1] or ()))
    return exits


def load_tables(fetch_rom, cache_path, fetch_digest=None):
    """Tables from cache when the ROM digest matches, else parse fetch_rom().

    fetch_digest() -> sha1 hex of the ROM file, or None (always
    fetch). Raises RomLayoutError/OSError on failure; callers fail closed.
    """
    cache_path = Path(cache_path)
    cached = None
    try:
        cached = json.loads(cache_path.read_text())
    except (OSError, ValueError):
        cached = None
    if cached and fetch_digest is not None:
        digest = fetch_digest()
        # A cache from before the script scan is rebuilt once.
        if digest and digest == cached.get("digest") and "scripted" in cached:
            return RomTables.from_json(cached)
    tables = RomTables.from_rom(fetch_rom())
    try:
        cache_path.parent.mkdir(parents=True, exist_ok=True)
        temporary = cache_path.with_suffix(".tmp")
        temporary.write_text(json.dumps(tables.to_json()))
        temporary.replace(cache_path)
    except OSError:
        pass
    return tables

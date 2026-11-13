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

    def __init__(self, maps, transitions, digest=None):
        # maps: {map_id: {"base": int, "entry": bool, "events": [int, ...]}}
        self.maps = maps
        self.transitions = transitions   # [(map, x, y, layer2, flags), ...]
        self.digest = digest

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
        return cls(maps, transitions, digest)

    def to_json(self):
        return {"digest": self.digest, "transitions": self.transitions,
                "maps": {str(m): info for m, info in self.maps.items()}}

    @classmethod
    def from_json(cls, data):
        return cls({int(m): info for m, info in data["maps"].items()},
                   [tuple(item) for item in data["transitions"]], data.get("digest"))

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
        if digest and digest == cached.get("digest"):
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

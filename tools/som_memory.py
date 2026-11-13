"""Read-only Secret of Mana WRAM observation and persistent movement history.

Input is a raw, contiguous 128-KiB SNES WRAM snapshot beginning at 7E0000.
Do not pass a Libretro serialized savestate: its layout is core-specific.
Field offsets below follow the public Super Famicom Development Wiki RAM map.
Enemy target validation follows the game's native target-list predicates.
"""
import json
import sqlite3
from collections import deque
import time
from pathlib import Path

BASE = 0x7E0000
SIZE = 0x20000
ENEMY_BASES = (0x7EE600, 0x7EE800, 0x7EEA00)
CHARACTER_BASES = (0x7EE000, 0x7EE200, 0x7EE400)
GRAPHICS = {0x80: "boy", 0x81: "girl", 0x82: "sprite"}
SOURCE = "https://wiki.superfamicom.org/seiken-densetsu-2"
TILE_BASES = (0x7F0000, 0x7F4000)
ATTRIBUTE_BASES = (0x7FB800, 0x7FBC00)
TILE_SIZE = 16
OPPOSITE_STEP = {"step_right": "step_left", "step_left": "step_right",
                 "step_up": "step_down", "step_down": "step_up",
                 "right": "left", "left": "right", "up": "down", "down": "up"}
ITEM_NAMES = ("candy", "medical_herb", "cup_of_wishes", "faerie_walnut",
              "royal_jam", "chocolate", "barrel", "magic_rope",
              "flammie_drum", "moogle_belt", "midge_mallet", "unknown_curative")


class WRAM:
    def __init__(self, data):
        if len(data) != SIZE:
            raise ValueError(f"Expected exactly {SIZE} raw WRAM bytes, got {len(data)}")
        self.data = bytes(data)

    @classmethod
    def load(cls, path):
        return cls(Path(path).read_bytes())

    def u8(self, addr):
        offset = addr - BASE
        if not 0 <= offset < SIZE:
            raise ValueError(f"Not a WRAM address: {addr:#08x}")
        return self.data[offset]

    def u16(self, addr):
        return self.u8(addr) | self.u8(addr + 1) << 8

    def character(self, index):
        if index not in (0, 1, 2):
            raise ValueError("Character slot must be 0..2")
        base = CHARACTER_BASES[index]
        # Character layout is 0x200 per slot. Availability flag semantics
        # are documented, but whether a slot is actively loaded also depends
        # on the game's event/party state.
        available = self.u8(base)
        graphic = self.u8(base + 0x180)
        hp = self.u16(base + 0x182)
        max_hp = self.u16(base + 0x184)
        return {
            "slot": index + 1,
            "availability_byte": available,
            "controller_binding": self.u8(base + 0x02C),
            "graphic": GRAPHICS.get(graphic, f"unknown_{graphic:02x}"),
            "level": self.u8(base + 0x181),
            "hp": hp, "max_hp": max_hp,
            "hp_plausible": 0 <= hp <= max_hp <= 999,
            "weapon_recharge": self.u8(base + 0x1ED),
            "native_ai_behavior": self.u8(0x7ECC73 + index),
            "native_ai_grid_cell": self.u8(0x7ECC7A + index),
            "native_ai_charge_level": self.u8(0x7ECC7D + index),
        }

    def game_mode(self):
        if self.u8(0x7EE000) != 1:
            return "no_game"
        if self.u8(0x7E00E4) != 1 or self.u8(0x7E00E6) != 0x0F:
            return "transition"
        if self.u8(0x7E00D0) != 0:
            return "event"
        if self.u8(0x7E007D) == 1:
            return "ring"
        return "field"

    def actor_position(self, index):
        if index not in (0, 1, 2):
            raise ValueError("Character slot must be 0..2")
        base = CHARACTER_BASES[index]
        layer_state = self.u8(base + 0x00B)
        return {
            "x_px": self.u16(base + 0x002),
            "y_px": self.u16(base + 0x004),
            "layer": 2 if layer_state & 0x80 else 1,
            "floor_height": layer_state & 3,
            "reported_tile_id": self.u8(base + 0x04C),
            "reported_ground_effect": self.u8(base + 0x048),
            "reported_priority": self.u8(base + 0x08E) & 7,
        }

    def tile(self, tile_x, tile_y, layer):
        if layer not in (1, 2):
            raise ValueError("Tile layer must be 1 or 2")
        # The live work map is 128 tile IDs per row. Coordinates are wrapped
        # by the caller to the current map's measured pixel dimensions.
        index = (tile_y & 0x7F) * 128 + (tile_x & 0x7F)
        tile_id = self.u8(TILE_BASES[layer - 1] + index)
        attribute_base = ATTRIBUTE_BASES[layer - 1] + tile_id * 4
        attributes = [self.u8(attribute_base + i) for i in range(4)]
        return tile_id, attributes

    @staticmethod
    def classify_tile(attributes, current_floor):
        """Can the hero, now at floor height current_floor, step onto this tile?

        Rules of the real movement code (cidwel/som-decomp docs/maps.md 7 and
        14.2, docs/hero-movement.md 7): a move is refused only into a type-3
        tile, a type-1 tile at height 2, or a type-2 tile at height 1. Types 0
        and 4-7 (bridge decks) never block; ramps/stairs (b2 0x20-0x2F) are
        type 0 and connect heights; layer-switch tiles skip the wall test;
        diagonal half tiles deflect (the hero slides along them). Only the
        moving floors and special ground stay conditional (not verified).
        """
        terrain = attributes[0] & 7
        ground = attributes[2]
        reasons = []
        if attributes[0] & 8:
            return "open", ["layer_switch"]
        if terrain == 3:
            return "blocked", ["solid"]
        if terrain in (1, 2) and current_floor in (1, 2) and terrain != current_floor:
            return "blocked", ["floor_height_wall"]
        if (ground & 0xF0) == 0x10 and (ground & 7) >= 4:
            reasons.append("diagonal_half_tile")
        if 0x20 <= ground <= 0x2F:
            reasons.append("ramp_or_stairs")
        elif 0x30 <= ground <= 0x3F:
            return "conditional", reasons + ["moving_floor"]
        elif ground in (0xD0, 0xD1, 0xD2):
            return "conditional", reasons + ["special_ground"]
        return "open", reasons

    @staticmethod
    def terrain_code(attributes):
        """Height-independent kind of a tile for multi-step route planning.

        '#' solid, '1'/'2' raised tile of that floor height, '=' pass-through
        (types 4-7: keeps the arrival height), '.' plain/ramp (height 0),
        '!' layer switch, '?' moving floor or special ground (unverified).
        """
        terrain, ground = attributes[0] & 7, attributes[2]
        if attributes[0] & 8:
            return "!"
        if terrain == 3:
            return "#"
        if 0x30 <= ground <= 0x3F or ground in (0xD0, 0xD1, 0xD2):
            return "?"
        if terrain in (1, 2):
            return str(terrain)
        return "=" if terrain >= 4 else "."

    def tile_context(self, actor_index=0, radius=3):
        if not 1 <= radius <= 6:
            raise ValueError("Tile radius must be 1..6")
        actor = self.actor_position(actor_index)
        width_px = self.u16(0x7E00C0)
        height_px = self.u16(0x7E00C2)
        dimensions_valid = (width_px % TILE_SIZE == 0 and height_px % TILE_SIZE == 0 and
                            TILE_SIZE <= width_px <= 2048 and
                            TILE_SIZE <= height_px <= 2048)
        result = {
            "profile": "som_us_engine_layout_candidate",
            "runtime_verified": False,
            "map_id": self.u16(0x7E00DC) & 0x1FF,
            "game_mode": self.game_mode(),
            "actor": actor,
            "tile_size_px": TILE_SIZE,
            "legend": {".": "open", "#": "blocked", "?": "conditional", "@": "actor"},
        }
        if not dimensions_valid:
            result["verification_error"] = "invalid_map_dimensions"
            return result
        if result["game_mode"] != "field":
            result["verification_error"] = "not_field_mode"
            return result
        width_tiles, height_tiles = width_px // TILE_SIZE, height_px // TILE_SIZE
        center_x = (actor["x_px"] // TILE_SIZE) % width_tiles
        center_y = (actor["y_px"] // TILE_SIZE) % height_tiles
        current_id, current_attributes = self.tile(center_x, center_y, actor["layer"])
        checks = {
            "tile_id": current_id == actor["reported_tile_id"],
            "floor_height": (current_attributes[0] & 3) == actor["floor_height"],
            "ground_effect": current_attributes[2] == actor["reported_ground_effect"],
            "priority": (current_attributes[1] & 7) == actor["reported_priority"],
        }
        result.update({
            "map_size_tiles": [width_tiles, height_tiles],
            "actor_tile": [center_x, center_y],
            "verification_checks": checks,
        })
        # Turbo can leave individual actor terrain caches stale. Require the
        # two movement-critical terrain properties plus either
        # independent identity or render priority. Turbo has now exhibited
        # both a stale tile-id cache on ordinary ground and a stale priority
        # cache on a verified special-ground/door tile.
        verified = (checks["floor_height"] and checks["ground_effect"] and
                    (checks["tile_id"] or checks["priority"]))
        if not verified:
            result["verification_error"] = "actor_tile_attributes_disagree"
            return result

        rows, tile_rows, attribute_rows, terrain_rows, special = [], [], [], [], []
        for dy in range(-radius, radius + 1):
            passability, ids, attribute_row, terrain_row = [], [], [], []
            for dx in range(-radius, radius + 1):
                x = (center_x + dx) % width_tiles
                y = (center_y + dy) % height_tiles
                tile_id, attributes = self.tile(x, y, actor["layer"])
                classification, reasons = self.classify_tile(attributes, actor["floor_height"])
                marker = {"open": ".", "blocked": "#", "conditional": "?"}[classification]
                if dx == 0 and dy == 0:
                    marker = "@"
                passability.append(marker)
                ids.append(f"{tile_id:02x}")
                attribute_row.append("".join(f"{value:02x}" for value in attributes))
                terrain_row.append(self.terrain_code(attributes))
                if reasons:
                    special.append({"offset": [dx, dy], "tile": tile_id,
                                    "attributes": "".join(f"{value:02x}" for value in attributes),
                                    "classification": classification, "reasons": reasons})
            rows.append("".join(passability))
            tile_rows.append(" ".join(ids))
            attribute_rows.append(" ".join(attribute_row))
            terrain_rows.append("".join(terrain_row))
        result.update({
            "runtime_verified": True,
            "verification_warning": (
                "stale_actor_tile_id_cache" if not checks["tile_id"] else
                "stale_actor_priority_cache" if not checks["priority"] else None),
            "radius": radius,
            "passability_rows": rows,
            "tile_id_rows": tile_rows,
            "attribute_rows": attribute_rows,
            "terrain_rows": terrain_rows,
            "special_cells": special,
        })
        return result

    def enemy(self, index):
        if index not in (0, 1, 2):
            raise ValueError("Enemy slot must be 0..2")
        base = ENEMY_BASES[index]
        hp = self.u16(base + 0x182)
        max_hp = self.u16(base + 0x184)
        active_byte = self.u8(base)
        identity = self.u8(base + 0x180)
        status = self.u16(base + 0x190)
        alive = not bool(status & 0x8000)
        # Native spell targeting accepts slots E600/E800/EA00 when +00 is
        # exactly 1, identity +180 is a monster (< 0x80), and +191 bit 7 is
        # clear. HP plausibility is reported separately, not used to invent
        # occupancy.
        targetable = active_byte == 1 and identity < 0x80 and alive
        return {
            "slot": index + 1,
            "active_byte": active_byte,
            "identity": identity,
            "x_px": self.u16(base + 2),
            "y_px": self.u16(base + 4),
            "hp": hp, "max_hp": max_hp,
            "hp_plausible": 0 < max_hp <= 9999 and hp <= max_hp,
            "status": status,
            "alive": alive,
            "active": targetable,
        }

    def inventory(self):
        """Decode the documented 11 item-ring slots without modifying WRAM."""
        items = []
        for slot in range(11):
            raw = self.u8(0x7ECC48 + slot)
            if raw == 0xFF:
                continue
            item_id, count_code = raw & 0x0F, raw >> 4
            # Normal inventory uses even high nibbles 2..8 for quantities 1..4.
            # Reject hacked/ambiguous values rather than invent availability.
            valid = item_id < len(ITEM_NAMES) and count_code in (2, 4, 6, 8)
            items.append({"slot": slot, "id": item_id,
                          "name": ITEM_NAMES[item_id] if item_id < len(ITEM_NAMES)
                          else f"unknown_{item_id:x}",
                          "count": count_code // 2 if valid else None,
                          "raw": raw, "verified": valid})
        return items

    def observe(self):
        tile_context = self.tile_context()
        inventory = self.inventory()
        return {
            "source": "raw_wram",
            "map_id": tile_context["map_id"],
            "player_position": tile_context["actor"] if tile_context["runtime_verified"] else None,
            "tile_context": tile_context,
            "weapons_enabled": bool(self.u8(0x7E00ED) & 0x80),
            "party": [self.character(i) for i in range(3)],
            "pad_bindings": [self.u8(0x7E00D9 + i) for i in range(3)],
            "enemy_slots": [self.enemy(i) for i in range(3)],
            "inventory": inventory,
            "candy_count": next((item["count"] for item in inventory
                                 if item["name"] == "candy" and item["verified"]), 0),
            "limitations": [
                "Enemy target validation covers the three native monster slots only",
                "Tile control is trusted only when three independent live actor fields agree",
                "Turbo-specific map changes still require hardware validation",
                "Companion availability alone is insufficient to force recruitment",
                "Candy quantity is decoded read-only; Turbo item-ring cursor is not verified"
            ],
        }


def grid_cells(state):
    """{(x, y): (marker, tile_id, attributes)} of a verified 7x7 grid, absolute."""
    context = state.get("tile_context") if isinstance(state, dict) else None
    if not isinstance(context, dict) or not context.get("runtime_verified"):
        return None
    rows, ids, attrs = (context.get("passability_rows"), context.get("tile_id_rows"),
                        context.get("attribute_rows"))
    radius, size, center = (context.get("radius"), context.get("map_size_tiles"),
                            context.get("actor_tile"))
    if not (isinstance(rows, list) and isinstance(ids, list) and isinstance(attrs, list)
            and isinstance(radius, int) and isinstance(size, list) and
            isinstance(center, list)):
        return None
    cells = {}
    for row_index, (markers, id_row, attr_row) in enumerate(zip(rows, ids, attrs)):
        for column, (marker, tile_id, attributes) in enumerate(
                zip(markers, id_row.split(), attr_row.split())):
            x = (center[0] + column - radius) % size[0]
            y = (center[1] + row_index - radius) % size[1]
            cells[(x, y)] = (marker, tile_id, attributes)
    return cells


def detect_cuts(previous, current):
    """Attributes of tiles next to the hero that turned from blocking to open.

    That is what cutting grass (or breaking a pot) looks like in WRAM: the
    tile in front of the hero is replaced by a different, walkable tile.
    """
    if not isinstance(previous, dict) or not isinstance(current, dict):
        return []
    before_context = previous.get("tile_context") or {}
    after_context = current.get("tile_context") or {}
    if (before_context.get("map_id") != after_context.get("map_id") or
            (before_context.get("actor") or {}).get("layer") !=
            (after_context.get("actor") or {}).get("layer")):
        return []
    before, after = grid_cells(previous), grid_cells(current)
    if not before or not after:
        return []
    hero = tuple(after_context["actor_tile"])
    size = after_context["map_size_tiles"]
    found = []
    for (x, y), (marker, tile_id, _) in after.items():
        old = before.get((x, y))
        if marker != "." or old is None or old[0] not in "#?" or old[1] == tile_id:
            continue
        dx = min((x - hero[0]) % size[0], (hero[0] - x) % size[0])
        dy = min((y - hero[1]) % size[1], (hero[1] - y) % size[1])
        if dx + dy == 1:
            found.append(old[2])
    return found


class ObservationHistory:
    """SQLite observations plus persistent, map-scoped tile visitation memory."""

    def __init__(self, path):
        self.path = Path(path)
        self.connection = sqlite3.connect(str(self.path))
        # Several small commits happen per decision. WAL with NORMAL sync
        # avoids a full disk flush for each one; at worst a power cut loses
        # the last few observations, never the database itself.
        self.connection.execute("PRAGMA journal_mode=WAL")
        self.connection.execute("PRAGMA synchronous=NORMAL")
        self.connection.execute(
            "CREATE TABLE IF NOT EXISTS observations "
            "(id INTEGER PRIMARY KEY, ts REAL NOT NULL, action TEXT, state TEXT NOT NULL)")
        self.connection.execute(
            "CREATE TABLE IF NOT EXISTS tile_visits "
            "(map_id INTEGER NOT NULL, layer INTEGER NOT NULL, x INTEGER NOT NULL, "
            "y INTEGER NOT NULL, visits INTEGER NOT NULL, last_ts REAL NOT NULL, "
            "PRIMARY KEY(map_id,layer,x,y))")
        self.connection.execute(
            "CREATE TABLE IF NOT EXISTS agent_meta "
            "(key TEXT PRIMARY KEY, value TEXT NOT NULL)")
        self.connection.execute(
            "CREATE TABLE IF NOT EXISTS world_cells "
            "(map_id INTEGER NOT NULL, layer INTEGER NOT NULL, x INTEGER NOT NULL, "
            "y INTEGER NOT NULL, kind TEXT NOT NULL, seen INTEGER NOT NULL, "
            "last_ts REAL NOT NULL, PRIMARY KEY(map_id,layer,x,y))")
        self.connection.execute(
            "CREATE TABLE IF NOT EXISTS combat_samples "
            "(id INTEGER PRIMARY KEY, ts REAL NOT NULL, map_id INTEGER, "
            "enemy_slot INTEGER, action TEXT, distance_px INTEGER, hero_hp INTEGER, "
            "enemy_hp INTEGER, damage_dealt INTEGER NOT NULL, "
            "damage_taken INTEGER NOT NULL, hero_level INTEGER)")
        self.connection.execute(
            "CREATE TABLE IF NOT EXISTS places "
            "(map_id INTEGER NOT NULL, layer INTEGER NOT NULL, x INTEGER NOT NULL, "
            "y INTEGER NOT NULL, kind TEXT NOT NULL, name TEXT NOT NULL, "
            "source TEXT NOT NULL, seen INTEGER NOT NULL, last_ts REAL NOT NULL, "
            "PRIMARY KEY(map_id,layer,x,y,kind))")
        # Tiles the hero actually stood on. Kept apart from world_cells so a
        # later "conditional"/"blocked" grid reading (bridges switch layers)
        # can never undo proof that the tile is walkable. Layer-agnostic.
        self.connection.execute(
            "CREATE TABLE IF NOT EXISTS walked_cells "
            "(map_id INTEGER NOT NULL, x INTEGER NOT NULL, y INTEGER NOT NULL, "
            "seen INTEGER NOT NULL, last_ts REAL NOT NULL, PRIMARY KEY(map_id,x,y))")
        # Collision attributes (4 bytes, hex) of conditional tiles the hero has
        # walked on. Tiles with identical attributes behave identically, so a
        # bridge crossed once makes every bridge with that signature usable.
        self.connection.execute(
            "CREATE TABLE IF NOT EXISTS passable_signatures "
            "(attributes TEXT PRIMARY KEY, seen INTEGER NOT NULL, last_ts REAL NOT NULL)")
        # Attributes of tiles seen turning from blocking to open next to the
        # hero (grass cut with a weapon). Same attributes = also cuttable.
        self.connection.execute(
            "CREATE TABLE IF NOT EXISTS cuttable_signatures "
            "(attributes TEXT PRIMARY KEY, seen INTEGER NOT NULL, last_ts REAL NOT NULL)")
        # 7x7 tile-ID patterns around taught inns, to recognise lookalikes.
        self.connection.execute(
            "CREATE TABLE IF NOT EXISTS place_patterns "
            "(kind TEXT NOT NULL, map_id INTEGER NOT NULL, pattern TEXT NOT NULL, "
            "PRIMARY KEY(kind,map_id))")
        self.connection.execute(
            "CREATE TABLE IF NOT EXISTS map_edges "
            "(from_map INTEGER NOT NULL, to_map INTEGER NOT NULL, layer INTEGER, "
            "x INTEGER NOT NULL, y INTEGER NOT NULL, action TEXT, "
            "seen INTEGER NOT NULL, last_ts REAL NOT NULL, PRIMARY KEY(from_map,to_map))")
        self.connection.commit()
        self._last_verified = None
        self._anchor_misses = 0
        if not self.connection.execute("SELECT 1 FROM map_edges LIMIT 1").fetchone():
            self._backfill_edges()

    @staticmethod
    def _verified_position(state, action):
        context = state.get("tile_context") if isinstance(state, dict) else None
        if not isinstance(context, dict) or not context.get("runtime_verified"):
            return None
        tile, actor = context.get("actor_tile"), context.get("actor") or {}
        if not isinstance(tile, list) or len(tile) != 2:
            return None
        return {"map_id": context.get("map_id"), "layer": actor.get("layer"),
                "target": list(tile), "action": action}

    def _learn_edge(self, previous, current):
        """Store both directions of one observed map crossing.

        Kept outside the trimmed observation log so a route home learned an
        hour ago is still known after thousands of later decisions.
        """
        if not previous or not current or previous["map_id"] == current["map_id"]:
            return
        now = time.time()
        reverse = OPPOSITE_STEP.get(previous["action"])
        for origin, destination, action in (
                (previous, current["map_id"], previous["action"]),
                (current, previous["map_id"], reverse)):
            self.connection.execute(
                "INSERT INTO map_edges(from_map,to_map,layer,x,y,action,seen,last_ts) "
                "VALUES(?,?,?,?,?,?,1,?) ON CONFLICT(from_map,to_map) DO UPDATE SET "
                "layer=excluded.layer,x=excluded.x,y=excluded.y,action=excluded.action,"
                "seen=seen+1,last_ts=excluded.last_ts",
                (origin["map_id"], destination, origin["layer"], *origin["target"],
                 action, now))

    def _backfill_edges(self):
        rows = self.connection.execute(
            "SELECT action,state FROM observations ORDER BY id ASC").fetchall()
        previous = None
        for action, encoded in rows:
            current = self._verified_position(json.loads(encoded), action)
            self._learn_edge(previous, current)
            previous = current or previous
        self._last_verified = previous
        self.connection.commit()

    def record(self, state, action=None):
        current = self._verified_position(state, action)
        if self._last_verified is None and current is not None:
            row = self.connection.execute(
                "SELECT action,state FROM observations ORDER BY id DESC LIMIT 1").fetchone()
            if row:
                self._last_verified = self._verified_position(json.loads(row[1]), row[0])
        self._learn_edge(self._last_verified, current)
        if current is not None:
            self._last_verified = current
        self.connection.execute(
            "INSERT INTO observations(ts,action,state) VALUES(?,?,?)",
            (time.time(), action, json.dumps(state, separators=(",", ":"))))
        self.connection.execute(
            "DELETE FROM observations WHERE id NOT IN "
            "(SELECT id FROM observations ORDER BY id DESC LIMIT 5000)")
        self.connection.commit()

    def recent(self, limit=3):
        rows = self.connection.execute(
            "SELECT action,state FROM observations ORDER BY id DESC LIMIT ?",
            (min(max(int(limit), 1), 20),)).fetchall()
        return [{"action": action, "state": json.loads(state)} for action, state in reversed(rows)]

    def remember_position(self, state):
        """Count a verified tile once per arrival, not once per AI tick."""
        context = state.get("tile_context") if isinstance(state, dict) else None
        if not isinstance(context, dict) or not context.get("runtime_verified"):
            return False
        tile = context.get("actor_tile")
        actor = context.get("actor")
        if not (isinstance(tile, list) and len(tile) == 2 and isinstance(actor, dict)):
            return False
        position = [context.get("map_id"), actor.get("layer"), tile[0], tile[1]]
        encoded = json.dumps(position, separators=(",", ":"))
        row = self.connection.execute(
            "SELECT value FROM agent_meta WHERE key='last_position'").fetchone()
        if row and row[0] == encoded:
            return False
        now = time.time()
        if row:
            previous = json.loads(row[0])
            if previous[0] != position[0]:
                self.remember_place(previous[0], previous[1], previous[2], previous[3],
                                    "exit", f"exit to map {position[0]}", "map_transition")
                self.remember_place(position[0], position[1], position[2], position[3],
                                    "entrance", f"entrance from map {previous[0]}",
                                    "map_transition")
        self.connection.execute(
            "INSERT INTO tile_visits(map_id,layer,x,y,visits,last_ts) VALUES(?,?,?,?,1,?) "
            "ON CONFLICT(map_id,layer,x,y) DO UPDATE SET "
            "visits=visits+1,last_ts=excluded.last_ts",
            (*position, now))
        self.connection.execute(
            "INSERT INTO agent_meta(key,value) VALUES('last_position',?) "
            "ON CONFLICT(key) DO UPDATE SET value=excluded.value", (encoded,))
        self.connection.commit()
        return True

    def remember_place(self, map_id, layer, x, y, kind, name, source="observed"):
        """Store a higher-level landmark independently from raw collision tiles."""
        if not all(isinstance(value, int) for value in (map_id, layer, x, y)):
            return False
        now = time.time()
        self.connection.execute(
            "INSERT INTO places(map_id,layer,x,y,kind,name,source,seen,last_ts) "
            "VALUES(?,?,?,?,?,?,?,1,?) ON CONFLICT(map_id,layer,x,y,kind) DO UPDATE SET "
            "name=excluded.name,source=excluded.source,seen=seen+1,last_ts=excluded.last_ts",
            (map_id, layer, x, y, str(kind), str(name), str(source), now))
        self.connection.commit()
        self.export_map_json()
        return True

    def place_summary(self, map_id=None):
        query = "SELECT map_id,layer,x,y,kind,name,source,seen FROM places"
        values = ()
        if map_id is not None:
            query += " WHERE map_id=?"
            values = (map_id,)
        query += " ORDER BY map_id,kind,y,x"
        return [{"map_id": row[0], "layer": row[1], "x": row[2], "y": row[3],
                 "kind": row[4], "name": row[5], "source": row[6], "seen": row[7]}
                for row in self.connection.execute(query, values).fetchall()]

    def export_map_json(self, path=None):
        """Export a human-readable, model-readable atlas beside the SQLite DB."""
        destination = Path(path) if path else self.path.with_suffix(".map.json")
        maps = {}
        for map_id, layer, x, y, kind, seen in self.connection.execute(
                "SELECT map_id,layer,x,y,kind,seen FROM world_cells "
                "ORDER BY map_id,layer,y,x"):
            entry = maps.setdefault(str(map_id), {"known_cells": [], "places": []})
            entry["known_cells"].append(
                {"layer": layer, "x": x, "y": y, "kind": kind, "seen": seen})
        for place in self.place_summary():
            maps.setdefault(str(place["map_id"]),
                            {"known_cells": [], "places": []})["places"].append(place)
        payload = {"format": "flxtr-som-atlas-v1", "maps": maps}
        temporary = destination.with_suffix(destination.suffix + ".tmp")
        temporary.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n",
                             encoding="utf-8")
        temporary.replace(destination)
        return destination

    def visit_count(self, map_id, layer, x, y):
        row = self.connection.execute(
            "SELECT visits FROM tile_visits WHERE map_id=? AND layer=? AND x=? AND y=?",
            (map_id, layer, x, y)).fetchone()
        return row[0] if row else 0

    def remember_world(self, state):
        """Merge the verified nearby collision grid into persistent map memory."""
        context = state.get("tile_context") if isinstance(state, dict) else None
        if not isinstance(context, dict) or not context.get("runtime_verified"):
            return 0
        rows, radius = context.get("passability_rows"), context.get("radius")
        size, center, actor = (context.get("map_size_tiles"),
                               context.get("actor_tile"), context.get("actor"))
        if not (isinstance(rows, list) and isinstance(radius, int) and
                isinstance(size, list) and len(size) == 2 and
                isinstance(center, list) and len(center) == 2 and
                isinstance(actor, dict)):
            return 0
        width, height = size
        if not (isinstance(width, int) and width > 0 and
                isinstance(height, int) and height > 0):
            return 0
        kinds = {".": "open", "@": "open", "#": "blocked", "?": "conditional"}
        # Height-independent kinds (som-decomp rules); the passability markers
        # above are relative to the hero's current height and are only the
        # fallback for observations without terrain rows.
        terrain_kinds = {"#": "blocked", "1": "h1", "2": "h2", "=": "pass",
                         ".": "open", "!": "open", "?": "conditional"}
        terrain_rows = context.get("terrain_rows")
        proven = self.passable_signatures()
        cuttable = self.cuttable_signatures()
        attribute_rows = context.get("attribute_rows") or []
        special = {tuple(cell["offset"]): cell.get("attributes")
                   for cell in context.get("special_cells") or []
                   if isinstance(cell.get("offset"), list)}
        now, learned = time.time(), 0
        for row_index, row in enumerate(rows):
            if not isinstance(row, str):
                continue
            for column, marker in enumerate(row):
                kind = kinds.get(marker)
                if kind is None:
                    continue
                if (isinstance(terrain_rows, list) and row_index < len(terrain_rows) and
                        column < len(terrain_rows[row_index])):
                    kind = terrain_kinds.get(terrain_rows[row_index][column], kind)
                if (kind == "conditional" and
                        special.get((column - radius, row_index - radius)) in proven):
                    kind = "open"
                attributes = (attribute_rows[row_index].split()[column]
                              if row_index < len(attribute_rows) else None)
                if kind in ("blocked", "conditional", "h1", "h2") and attributes in cuttable:
                    kind = "cuttable"
                x = (center[0] + column - radius) % width
                y = (center[1] + row_index - radius) % height
                self.connection.execute(
                    "INSERT INTO world_cells(map_id,layer,x,y,kind,seen,last_ts) "
                    "VALUES(?,?,?,?,?,1,?) ON CONFLICT(map_id,layer,x,y) DO UPDATE SET "
                    "kind=excluded.kind,seen=seen+1,last_ts=excluded.last_ts",
                    (context["map_id"], actor["layer"], x, y, kind, now))
                learned += 1
        self.connection.commit()
        return learned

    def known_cells(self, map_id, layer):
        rows = self.connection.execute(
            "SELECT x,y,kind FROM world_cells WHERE map_id=? AND layer=?",
            (map_id, layer)).fetchall()
        cells = {(x, y): kind for x, y, kind in rows}
        for position in self.walked_cells(map_id):
            cells[position] = "open"
        return cells

    @staticmethod
    def walk_position(state):
        """(map, x, y) under the hero in field mode, even if the tile-ID
        cross-check failed (it does on some bridges); None otherwise."""
        context = state.get("tile_context") if isinstance(state, dict) else None
        if not isinstance(context, dict) or context.get("game_mode") != "field":
            return None
        tile = context.get("actor_tile")
        if not isinstance(tile, list) or len(tile) != 2:
            return None
        return context.get("map_id"), tile[0], tile[1]

    def remember_walk(self, state):
        position = self.walk_position(state)
        if position is None:
            return False
        context = state["tile_context"]
        for cell in context.get("special_cells") or []:
            if (cell.get("offset") == [0, 0] and cell.get("classification") == "conditional"
                    and isinstance(cell.get("attributes"), str)):
                self.connection.execute(
                    "INSERT INTO passable_signatures(attributes,seen,last_ts) VALUES(?,1,?) "
                    "ON CONFLICT(attributes) DO UPDATE SET seen=seen+1,last_ts=excluded.last_ts",
                    (cell["attributes"], time.time()))
        self.connection.execute(
            "INSERT INTO walked_cells(map_id,x,y,seen,last_ts) VALUES(?,?,?,1,?) "
            "ON CONFLICT(map_id,x,y) DO UPDATE SET seen=seen+1,last_ts=excluded.last_ts",
            (*position, time.time()))
        self.connection.commit()
        return True

    def learn_cuts(self, state):
        """Compare with the previous observation; store newly seen cut tiles."""
        previous, self._previous_state = getattr(self, "_previous_state", None), state
        learned = detect_cuts(previous, state)
        for attributes in learned:
            self.connection.execute(
                "INSERT INTO cuttable_signatures(attributes,seen,last_ts) VALUES(?,1,?) "
                "ON CONFLICT(attributes) DO UPDATE SET seen=seen+1,last_ts=excluded.last_ts",
                (attributes, time.time()))
        if learned:
            self.connection.commit()
        return learned

    def cuttable_signatures(self):
        return {row[0] for row in self.connection.execute(
            "SELECT attributes FROM cuttable_signatures")}

    def passable_signatures(self):
        return {row[0] for row in self.connection.execute(
            "SELECT attributes FROM passable_signatures")}

    def remember_pattern(self, kind, state):
        context = state.get("tile_context") or {}
        rows = context.get("tile_id_rows")
        if not isinstance(rows, list) or not context.get("runtime_verified"):
            return False
        self.connection.execute(
            "INSERT INTO place_patterns(kind,map_id,pattern) VALUES(?,?,?) "
            "ON CONFLICT(kind,map_id) DO UPDATE SET pattern=excluded.pattern",
            (kind, context["map_id"], json.dumps(rows)))
        self.connection.commit()
        return True

    def looks_like(self, kind, state, threshold=0.8):
        """Fraction-matched taught pattern of this kind on another map, or None."""
        context = state.get("tile_context") or {}
        rows = context.get("tile_id_rows")
        if not isinstance(rows, list) or not context.get("runtime_verified"):
            return None
        here = [cell for row in rows for cell in row.split()]
        best = None
        for map_id, encoded in self.connection.execute(
                "SELECT map_id,pattern FROM place_patterns WHERE kind=?", (kind,)):
            if map_id == context.get("map_id"):
                continue
            known = [cell for row in json.loads(encoded) for cell in row.split()]
            if len(known) != len(here):
                continue
            score = sum(a == b for a, b in zip(here, known)) / len(here)
            if score >= threshold and (best is None or score > best):
                best = score
        return best

    def walked_cells(self, map_id):
        return {(x, y) for x, y in self.connection.execute(
            "SELECT x,y FROM walked_cells WHERE map_id=?", (map_id,))}

    def get_agent_state(self, key):
        row = self.connection.execute(
            "SELECT value FROM agent_meta WHERE key=?", (key,)).fetchone()
        return json.loads(row[0]) if row else None

    def set_agent_state(self, key, value):
        if value is None:
            self.connection.execute("DELETE FROM agent_meta WHERE key=?", (key,))
        else:
            self.connection.execute(
                "INSERT INTO agent_meta(key,value) VALUES(?,?) "
                "ON CONFLICT(key) DO UPDATE SET value=excluded.value",
                (key, json.dumps(value, separators=(",", ":"))))
        self.connection.commit()

    @staticmethod
    def _safe_position(state):
        """Return a verified position suitable for a recovery anchor."""
        context = state.get("tile_context") if isinstance(state, dict) else None
        party = state.get("party") or [] if isinstance(state, dict) else []
        hero = party[0] if party else {}
        tile = context.get("actor_tile") if isinstance(context, dict) else None
        actor = context.get("actor") if isinstance(context, dict) else None
        enemies = [enemy for enemy in state.get("enemy_slots", [])
                   if isinstance(enemy, dict) and enemy.get("active")]
        if (not isinstance(context, dict) or not context.get("runtime_verified") or
                not isinstance(tile, list) or len(tile) != 2 or
                not isinstance(actor, dict) or enemies or
                not hero.get("max_hp") or hero.get("hp") != hero.get("max_hp")):
            return None
        return {"map_id": context.get("map_id"), "layer": actor.get("layer"),
                "target": list(tile)}

    def safe_anchor(self):
        """Return the learned inn/safe position, bootstrapping old runs once."""
        anchor = self.get_agent_state("safe_anchor")
        if isinstance(anchor, dict):
            return anchor
        # Scanning the log is expensive; retry only occasionally.
        self._anchor_misses += 1
        if self._anchor_misses % 200 != 1:
            return None
        # Prefer an early full-health, enemy-free Potos observation. This makes
        # existing training databases useful immediately without hardcoding a
        # doorway coordinate. A later observed heal replaces it with the real
        # recovery location.
        fallback = None
        potos_fallback = None
        rows = self.connection.execute(
            "SELECT state FROM observations ORDER BY id ASC LIMIT 5000").fetchall()
        for (encoded,) in rows:
            state = json.loads(encoded)
            context = state.get("tile_context") or {}
            actor = context.get("actor") or {}
            tile = context.get("actor_tile")
            if (potos_fallback is None and context.get("runtime_verified") and
                    context.get("map_id") == 131 and isinstance(tile, list) and
                    len(tile) == 2 and isinstance(actor.get("layer"), int)):
                potos_fallback = {"map_id": 131, "layer": actor["layer"],
                                  "target": list(tile)}
            candidate = self._safe_position(state)
            if candidate is None:
                continue
            fallback = fallback or candidate
            if candidate.get("map_id") == 131:
                fallback = candidate
                break
        fallback = fallback or potos_fallback
        if fallback is not None:
            self.set_agent_state("safe_anchor", fallback)
            self.remember_place(fallback["map_id"], fallback["layer"],
                                *fallback["target"], "safe", "learned safe return point",
                                "full_health_observation")
        return fallback

    def remember_recovery(self, state):
        """Learn the exact inn tile when health rises in an enemy-free area."""
        party = state.get("party") or [] if isinstance(state, dict) else []
        hero = party[0] if party else {}
        previous = self.get_agent_state("health_previous") or {}
        candidate = self._safe_position(state)
        candy = state.get("candy_count") if isinstance(state, dict) else None
        used_candy = (isinstance(candy, int) and isinstance(previous.get("candy"), int) and
                      candy < previous["candy"])
        anchor = self.get_agent_state("safe_anchor")
        taught = isinstance(anchor, dict) and anchor.get("taught")
        # A Candy heal is not an inn, and a taught inn is never replaced.
        healed = (candidate is not None and not used_candy and not taught and
                  isinstance(previous.get("hp"), int) and
                  isinstance(hero.get("hp"), int) and hero["hp"] > previous["hp"])
        if healed:
            self.set_agent_state("safe_anchor", candidate)
            self.remember_place(candidate["map_id"], candidate["layer"],
                                *candidate["target"], "inn", "observed recovery point",
                                "health_increase")
        self.set_agent_state("health_previous", {"hp": hero.get("hp"),
                                                   "max_hp": hero.get("max_hp"),
                                                   "candy": candy})
        return candidate if healed else None

    def teach_inn(self, state, face):
        """Store the tile in front of the innkeeper shown by the player."""
        position = self._verified_position(state, None)
        if position is None:
            raise ValueError("the tile under the hero is not runtime-verified")
        if face not in ("step_up", "step_down", "step_left", "step_right"):
            raise ValueError(f"unknown facing {face!r}")
        anchor = {"map_id": position["map_id"], "layer": position["layer"],
                  "target": position["target"], "face": face, "taught": True}
        self.set_agent_state("safe_anchor", anchor)
        self.remember_pattern("inn", state)
        self.remember_place(anchor["map_id"], anchor["layer"], *anchor["target"],
                            "inn", "taught innkeeper tile", "player_demonstration")
        return anchor

    def teach_shop(self, state, face, buttons, price, gold):
        """Store the shopkeeper tile, facing, recorded purchase and gold byte."""
        position = self._verified_position(state, None)
        if position is None:
            raise ValueError("the tile under the hero is not runtime-verified")
        shop = {"map_id": position["map_id"], "layer": position["layer"],
                "target": position["target"], "face": face, "buttons": list(buttons),
                "price": int(price), "item": "candy", "taught": True}
        self.set_agent_state("shop", shop)
        self.set_agent_state("gold", gold)
        self.remember_place(shop["map_id"], shop["layer"], *shop["target"],
                            "shop", "taught Candy shop", "player_demonstration")
        return shop

    def return_waypoint(self, current_map, target_map):
        """First exit on the shortest learned chain of maps to target_map."""
        edges = {}
        for from_map, to_map, layer, x, y, action in self.connection.execute(
                "SELECT from_map,to_map,layer,x,y,action FROM map_edges"):
            edges.setdefault(from_map, []).append((to_map, layer, x, y, action))
        first_hop = {current_map: None}
        queue = deque([current_map])
        while queue:
            here = queue.popleft()
            for to_map, layer, x, y, action in edges.get(here, []):
                if to_map in first_hop:
                    continue
                first_hop[to_map] = first_hop[here] or {
                    "map_id": current_map, "layer": layer, "target": [x, y],
                    "action": action, "next_map": to_map}
                if to_map == target_map:
                    return dict(first_hop[to_map])
                queue.append(to_map)
        return self._scan_return_waypoint(current_map, target_map)

    def _scan_return_waypoint(self, current_map, target_map):
        """Legacy direct-edge scan of the recent observation log."""
        previous = None
        rows = self.connection.execute(
            "SELECT action,state FROM observations ORDER BY id ASC LIMIT 5000").fetchall()
        opposites = {"step_right": "step_left", "step_left": "step_right",
                     "step_up": "step_down", "step_down": "step_up"}
        result = None
        for action, encoded in rows:
            state = json.loads(encoded)
            context = state.get("tile_context") or {}
            actor = context.get("actor") or {}
            tile = context.get("actor_tile")
            current = ({"map_id": context.get("map_id"), "layer": actor.get("layer"),
                        "target": tile, "action": action}
                       if context.get("runtime_verified") and
                       isinstance(tile, list) and len(tile) == 2 else None)
            if (previous and current and previous["map_id"] != current["map_id"]):
                if previous["map_id"] == current_map and current["map_id"] == target_map:
                    result = {"map_id": current_map, "layer": previous["layer"],
                              "target": previous["target"],
                              "action": previous["action"]}
                elif current["map_id"] == current_map and previous["map_id"] == target_map:
                    result = {"map_id": current_map, "layer": current["layer"],
                              "target": current["target"],
                              "action": opposites.get(previous["action"])}
            previous = current or previous
        return result

    def record_combat(self, state, previous_action=None):
        """Persist observed fight outcomes attributable to the previous action."""
        if not isinstance(state, dict):
            return 0
        party = state.get("party") or []
        hero = party[0] if party else {}
        enemies = {enemy.get("slot"): enemy for enemy in state.get("enemy_slots", [])
                   if isinstance(enemy, dict) and enemy.get("active")}
        previous = self.get_agent_state("combat_previous") or {}
        prior_enemies = previous.get("enemies", {})
        player = state.get("player_position") or {}
        inserted = 0
        for slot, enemy in enemies.items():
            prior = prior_enemies.get(str(slot), {})
            same_map = previous.get("map_id") == state.get("map_id")
            damage_dealt = max(0, prior.get("hp", enemy.get("hp", 0)) - enemy.get("hp", 0)) if same_map else 0
            damage_taken = max(0, previous.get("hero_hp", hero.get("hp", 0)) - hero.get("hp", 0)) if same_map else 0
            distance = None
            if player and enemy.get("x_px") is not None and enemy.get("y_px") is not None:
                distance = abs(enemy["x_px"] - player.get("x_px", enemy["x_px"])) + \
                           abs(enemy["y_px"] - player.get("y_px", enemy["y_px"]))
            self.connection.execute(
                "INSERT INTO combat_samples(ts,map_id,enemy_slot,action,distance_px,"
                "hero_hp,enemy_hp,damage_dealt,damage_taken,hero_level) "
                "VALUES(?,?,?,?,?,?,?,?,?,?)",
                (time.time(), state.get("map_id"), slot, previous_action, distance,
                 hero.get("hp"), enemy.get("hp"), damage_dealt, damage_taken,
                 hero.get("level")))
            inserted += 1
        snapshot = {"map_id": state.get("map_id"), "hero_hp": hero.get("hp"),
                    "hero_level": hero.get("level"),
                    "enemies": {str(slot): {"hp": enemy.get("hp")} for slot, enemy in enemies.items()}}
        self.set_agent_state("combat_previous", snapshot)
        self.connection.commit()
        return inserted

    def combat_summary(self):
        rows = self.connection.execute(
            "SELECT COALESCE(action,'none'),COUNT(*),SUM(damage_dealt),SUM(damage_taken),"
            "MIN(distance_px),MAX(hero_level) FROM combat_samples GROUP BY action "
            "ORDER BY COUNT(*) DESC LIMIT 8").fetchall()
        return [{"action": action, "samples": samples, "damage_dealt": dealt or 0,
                 "damage_taken": taken or 0, "closest_px": closest,
                 "max_level": level}
                for action, samples, dealt, taken, closest, level in rows]

    def close(self):
        self.connection.close()

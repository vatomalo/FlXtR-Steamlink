"""Read-only Secret of Mana WRAM observation and persistent movement history.

Input is a raw, contiguous 128-KiB SNES WRAM snapshot beginning at 7E0000.
Do not pass a Libretro serialized savestate: its layout is core-specific.
Field offsets below follow the public Super Famicom Development Wiki RAM map.
Enemy target validation follows the game's native target-list predicates.
"""
import json
import sqlite3
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
        terrain = attributes[0] & 7
        ground = attributes[2]
        reasons = []
        if terrain == 3:
            return "blocked", ["solid"]
        if terrain in (1, 2) and current_floor in (1, 2) and terrain != current_floor:
            return "blocked", ["floor_height_wall"]
        if attributes[0] & 8:
            reasons.append("layer_switch")
        if (ground & 0xF0) == 0x10 and (ground & 7) >= 4:
            reasons.append("partial_collision_shape")
        if 0x20 <= ground <= 0x2F:
            reasons.append("ramp_or_stairs")
        elif 0x30 <= ground <= 0x3F:
            reasons.append("moving_floor")
        elif ground in (0xD0, 0xD1, 0xD2):
            reasons.append("special_ground")
        return ("conditional" if reasons else "open"), reasons

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
        # Turbo can leave actor +04C's tile-id cache stale even while the
        # terrain fields used by movement are current. Live map 131 showed
        # cached id 148 at map id 160, but floor, ground and priority all
        # agreed. Trust collision data only when those three independent
        # semantic fields match; retain tile_id as a diagnostic check.
        semantic_checks = ("floor_height", "ground_effect", "priority")
        if not all(checks[name] for name in semantic_checks):
            result["verification_error"] = "actor_tile_attributes_disagree"
            return result

        rows, tile_rows, special = [], [], []
        for dy in range(-radius, radius + 1):
            passability, ids = [], []
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
                if reasons:
                    special.append({"offset": [dx, dy], "tile": tile_id,
                                    "attributes": "".join(f"{value:02x}" for value in attributes),
                                    "classification": classification, "reasons": reasons})
            rows.append("".join(passability))
            tile_rows.append(" ".join(ids))
        result.update({
            "runtime_verified": True,
            "verification_warning": (None if checks["tile_id"] else
                                     "stale_actor_tile_id_cache"),
            "radius": radius,
            "passability_rows": rows,
            "tile_id_rows": tile_rows,
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

    def observe(self):
        tile_context = self.tile_context()
        return {
            "source": "raw_wram",
            "map_id": tile_context["map_id"],
            "player_position": tile_context["actor"] if tile_context["runtime_verified"] else None,
            "tile_context": tile_context,
            "weapons_enabled": bool(self.u8(0x7E00ED) & 0x80),
            "party": [self.character(i) for i in range(3)],
            "pad_bindings": [self.u8(0x7E00D9 + i) for i in range(3)],
            "enemy_slots": [self.enemy(i) for i in range(3)],
            "limitations": [
                "Enemy target validation covers the three native monster slots only",
                "Tile control is trusted only when three independent live actor fields agree",
                "Turbo-specific map changes still require hardware validation",
                "Companion availability alone is insufficient to force recruitment"
            ],
        }


class ObservationHistory:
    """SQLite observations plus persistent, map-scoped tile visitation memory."""

    def __init__(self, path):
        self.connection = sqlite3.connect(str(path))
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
        self.connection.commit()

    def record(self, state, action=None):
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
        now, learned = time.time(), 0
        for row_index, row in enumerate(rows):
            if not isinstance(row, str):
                continue
            for column, marker in enumerate(row):
                kind = kinds.get(marker)
                if kind is None:
                    continue
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
        return {(x, y): kind for x, y, kind in rows}

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

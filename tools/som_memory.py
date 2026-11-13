"""Read-only Secret of Mana WRAM observation and persistent movement history.

Input is a raw, contiguous 128-KiB SNES WRAM snapshot beginning at 7E0000.
Do not pass a Libretro serialized savestate: its layout is core-specific.
Field offsets below follow the public Super Famicom Development Wiki RAM map.
Unverified player coordinates and enemy slot-activation flags are NOT guessed.
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
            "graphic": GRAPHICS.get(graphic, f"unknown_{graphic:02x}"),
            "level": self.u8(base + 0x181),
            "hp": hp, "max_hp": max_hp,
            "hp_plausible": 0 <= hp <= max_hp <= 999,
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
            "ground_effect": current_attributes[2] == actor["reported_ground_effect"],
            "priority": (current_attributes[1] & 7) == actor["reported_priority"],
        }
        result.update({
            "map_size_tiles": [width_tiles, height_tiles],
            "actor_tile": [center_x, center_y],
            "verification_checks": checks,
        })
        if not all(checks.values()):
            result["verification_error"] = "actor_tile_fields_disagree"
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
        return {
            "slot": index + 1,
            "x_raw": self.u16(base + 4),
            "y_raw": self.u16(base + 2),
            "hp": hp, "max_hp": max_hp,
            "hp_plausible": 0 < max_hp <= 9999 and hp <= max_hp,
            "active": None,  # no verified active-slot flag yet
        }

    def observe(self):
        tile_context = self.tile_context()
        return {
            "source": "raw_wram",
            "map_id": tile_context["map_id"],
            "player_position": tile_context["actor"] if tile_context["runtime_verified"] else None,
            "tile_context": tile_context,
            "weapons_enabled": self.u8(0x7E00ED) == 0x80,
            "party": [self.character(i) for i in range(3)],
            "enemy_slots": [self.enemy(i) for i in range(3)],
            "limitations": [
                "Enemy slot occupancy and coordinate units unverified",
                "Tile control is trusted only when three independent live actor fields agree",
                "Turbo-specific map changes still require hardware validation",
                "Companion availability alone is insufficient to force recruitment"
            ],
        }


class ObservationHistory:
    """SQLite ring of observations; future verified coordinates can drive pathfinding."""

    def __init__(self, path):
        self.connection = sqlite3.connect(str(path))
        self.connection.execute(
            "CREATE TABLE IF NOT EXISTS observations "
            "(id INTEGER PRIMARY KEY, ts REAL NOT NULL, action TEXT, state TEXT NOT NULL)")
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

    def close(self):
        self.connection.close()

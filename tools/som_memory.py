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
        return {
            "source": "raw_wram",
            "map_id": None,  # do not confuse exit destination with current map
            "player_position": None,  # must be verified on actual ROM/core
            "weapons_enabled": self.u8(0x7E00ED) == 0x80,
            "party": [self.character(i) for i in range(3)],
            "enemy_slots": [self.enemy(i) for i in range(3)],
            "limitations": [
                "Enemy slot occupancy and coordinate units unverified",
                "Player position, collision map, and current map ID not yet decoded",
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

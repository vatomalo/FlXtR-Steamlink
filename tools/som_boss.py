"""Record boss monsters from live WRAM so they can be studied and summoned later.

Read-only. The first time an active monster with max HP >= BOSS_MIN_HP is
seen (per identity, at full HP when possible), its 0x200-byte object
(monster slots 7EE600/E800/EA00, cidwel/som-decomp) and the whole 128 KiB
WRAM snapshot are written to ~/.local/state/flxtr/som-boss/ with a JSON
index entry. A later sighting at full HP replaces a record captured hurt.

Summoning is NOT done here: writing a recorded object into another map
without the boss's graphics, palette and script loaded is unverified (see
docs/SOM-AI-HANDOFF.md, "Boss records").
"""
import hashlib
import json
import time
from pathlib import Path

from som_memory import ENEMY_BASES

BOSS_MIN_HP = 200
OBJECT_SIZE = 0x200
DEFAULT_DIR = Path.home() / ".local/state/flxtr/som-boss"


class BossRecorder:
    def __init__(self, directory=DEFAULT_DIR, log=print, clock=time.time):
        self.dir, self.log, self.clock = Path(directory), log, clock
        self.dir.mkdir(parents=True, exist_ok=True)
        self.index_path = self.dir / "index.json"
        try:
            self.index = json.loads(self.index_path.read_text())
        except (OSError, ValueError):
            self.index = {}

    def observe(self, raw, wram):
        """Record new bosses in this snapshot; returns identities recorded."""
        recorded = []
        map_id = wram.u16(0x7E00DC) & 0x1FF
        for slot in range(3):
            enemy = wram.enemy(slot)
            if not enemy["active_byte"] or enemy["max_hp"] < BOSS_MIN_HP or enemy["hp"] <= 0:
                continue
            key = f"{enemy['identity']:02x}"
            old = self.index.get(key)
            full = enemy["hp"] >= enemy["max_hp"]
            if old and (old.get("full_hp") or not full):
                continue
            base = ENEMY_BASES[slot] - 0x7E0000
            obj = bytes(raw[base:base + OBJECT_SIZE])
            stamp = time.strftime("%Y%m%d-%H%M%S", time.localtime(self.clock()))
            obj_file = f"boss-{key}-{stamp}.obj"
            wram_file = f"wram-{key}-{stamp}.bin"
            (self.dir / obj_file).write_bytes(obj)
            (self.dir / wram_file).write_bytes(bytes(raw))
            self.index[key] = {
                "identity": enemy["identity"], "map_id": map_id, "slot": slot,
                "object_base": f"{ENEMY_BASES[slot]:#08x}", "hp": enemy["hp"],
                "max_hp": enemy["max_hp"], "x_px": enemy["x_px"], "y_px": enemy["y_px"],
                "full_hp": full, "object_file": obj_file, "wram_file": wram_file,
                "sha1": hashlib.sha1(obj).hexdigest(), "captured": stamp}
            self.index_path.write_text(json.dumps(self.index, indent=1, sort_keys=True))
            self.log(f"[boss] recorded monster {key} on map {map_id} "
                     f"(HP {enemy['hp']}/{enemy['max_hp']}) -> {obj_file}")
            recorded.append(enemy["identity"])
        return recorded

"""Closed-loop ring-menu macros for Secret of Mana Turbo (Candy first).

Every button press is followed by a fresh WRAM read, and the macro proceeds
only when the expected state change is observed. Anything unexpected closes
the ring with Cancel and reports failure. Nothing here writes game memory.

The ring cursor byte is not documented for the Turbo ROM, so it is learned
once by tools/som_ring_probe.py and stored in ~/.config/flxtr/som-ring.json.
"""
import json
import os
import time
from pathlib import Path

from som_memory import WRAM

RING_OPEN = 0x7E007D
RING_KIND = 0x7EA20F
BANK_7E = range(0x7E0000, 0x7F0000)
DEFAULT_CALIBRATION = Path(os.environ.get("XDG_CONFIG_HOME", Path.home() / ".config")) \
    / "flxtr" / "som-ring.json"


def ring_open(wram):
    return wram.u8(RING_OPEN) == 1


def candy_slot(wram):
    """Inventory slot holding verified Candy, or None."""
    for item in wram.inventory():
        if item["name"] == "candy" and item["verified"] and item["count"]:
            return item["slot"]
    return None


def find_cursor_candidates(snapshots, items):
    """Addresses whose value advances by one ring position per snapshot.

    snapshots: WRAM objects taken with the item ring open, each after one
    rotation in the same direction. items: number of items in the ring.
    Returns [(address, step)] where step is +1 or -1 per rotation.
    """
    if len(snapshots) < 3 or items < 2:
        return []
    found = []
    for address in BANK_7E:
        values = [snapshot.u8(address) for snapshot in snapshots]
        if any(value >= items for value in values):
            continue
        steps = {(after - before) % items for before, after in zip(values, values[1:])}
        if len(steps) != 1:
            continue
        step = steps.pop()
        if step == 1:
            found.append((address, 1))
        elif step == items - 1:
            found.append((address, -1))
    return found


class Calibration:
    def __init__(self, data):
        self.open_action = data["open_action"]
        self.cancel_action = data.get("cancel_action", "cancel")
        self.confirm_action = data.get("confirm_action", "confirm")
        self.item_ring_kind = int(data["item_ring_kind"])
        # None: calibrated with a one-item ring, which never rotates.
        self.cursor_address = (None if data["cursor_address"] is None
                               else int(data["cursor_address"]))
        self.item_types = int(data.get("item_types", 0))
        self.cursor_step = int(data["cursor_step"])
        self.rotate_action = data.get("rotate_action", "step_right")
        self.candy_cursor = int(data["candy_cursor"])
        self.candy_slot = int(data["candy_slot"])

    @classmethod
    def load(cls, path=DEFAULT_CALIBRATION):
        try:
            return cls(json.loads(Path(path).read_text()))
        except (OSError, ValueError, KeyError, TypeError):
            return None

    def to_json(self):
        return {"open_action": self.open_action, "cancel_action": self.cancel_action,
                "confirm_action": self.confirm_action,
                "item_ring_kind": self.item_ring_kind,
                "cursor_address": self.cursor_address, "cursor_step": self.cursor_step,
                "rotate_action": self.rotate_action,
                "candy_cursor": self.candy_cursor, "candy_slot": self.candy_slot,
                "item_types": self.item_types}


class MacroFailed(Exception):
    pass


class CandyMacro:
    """Open the item ring, select Candy, use it on the leader, verify healing."""

    POLL = 0.05

    def __init__(self, link, calibration, log=print, sleep=time.sleep, clock=time.monotonic):
        self.link, self.cal, self.log = link, calibration, log
        self.sleep, self.clock = sleep, clock

    def _read(self):
        raw = self.link.fetch_wram(True)
        if raw is None:
            raise MacroFailed("physical takeover or game exit")
        return WRAM(raw)

    def _press_until(self, action, check, timeout=1.0, what="state change"):
        self.link.send_action(action)
        deadline = self.clock() + timeout
        while True:
            self.sleep(self.POLL)
            wram = self._read()
            if check(wram):
                return wram
            if self.clock() >= deadline:
                raise MacroFailed(f"{action} did not produce {what}")

    def _close_ring(self):
        for _ in range(4):
            try:
                wram = self._read()
            except (MacroFailed, OSError):
                return
            if not ring_open(wram):
                return
            self.link.send_action(self.cal.cancel_action)
            self.sleep(0.25)

    def ready(self, wram):
        """Why Candy cannot be used now, or None when it can."""
        if wram.game_mode() != "field":
            return "not in field mode"
        slot = candy_slot(wram)
        if slot is None:
            return "no Candy in inventory"
        if slot != self.cal.candy_slot:
            return f"Candy moved to inventory slot {slot}; recalibrate the ring"
        if self.cal.cursor_address is None:
            types = len([item for item in wram.inventory() if item["verified"]])
            if types != 1:
                return ("calibrated with Candy as the only item; you now carry "
                        f"{types} item types, rerun tools/som_ring_probe.py")
        return None

    def run(self):
        start = self._read()
        why = self.ready(start)
        if why:
            raise MacroFailed(why)
        hero_hp = start.character(0)["hp"]
        candy_count = next(item["count"] for item in start.inventory()
                           if item["name"] == "candy" and item["verified"])
        try:
            wram = self._press_until(self.cal.open_action, ring_open, what="an open ring")
            if wram.u8(RING_KIND) != self.cal.item_ring_kind:
                raise MacroFailed(f"ring opened on kind {wram.u8(RING_KIND)}, "
                                  f"not the item ring {self.cal.item_ring_kind}")
            address = self.cal.cursor_address
            for _ in range(16 if address is not None else 0):
                cursor = wram.u8(address)
                if cursor == self.cal.candy_cursor:
                    break
                before = cursor
                wram = self._press_until(self.cal.rotate_action,
                                         lambda w: w.u8(address) != before,
                                         timeout=0.8, what="ring rotation")
            else:
                if address is not None:
                    raise MacroFailed("Candy not reached after 16 ring rotations")
            self.log("[candy] Candy selected; confirming")
            # Item select, then target select (leader is the default target).
            self.link.send_action(self.cal.confirm_action)
            self.sleep(0.35)
            self.link.send_action(self.cal.confirm_action)
            deadline = self.clock() + 3.0
            while self.clock() < deadline:
                self.sleep(0.1)
                wram = self._read()
                hp = wram.character(0)["hp"]
                count = next((item["count"] for item in wram.inventory()
                              if item["name"] == "candy" and item["verified"]), 0)
                if hp > hero_hp or count < candy_count:
                    self._close_ring()
                    return hp - hero_hp
            raise MacroFailed("no HP gain or Candy use observed")
        except MacroFailed:
            self._close_ring()
            raise

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
        """Close a lingering item ring and VERIFY the return to field mode.

        Four cancels were insufficient on live Turbo, leaving the agent
        permanently waiting in ring mode after "Candy failed". Never send
        buttons in an unrelated event/dialog/transition.
        """
        for _ in range(12):
            wram = self._read()
            if wram.game_mode() == "field":
                return True
            if not ring_open(wram):
                raise MacroFailed(
                    f"Candy cleanup blocked: game mode {wram.game_mode()}, "
                    "not an item ring; refusing blind input")
            self.link.send_action(self.cal.cancel_action)
            self.sleep(0.35)
        if self._read().game_mode() == "field":
            return True
        raise MacroFailed("Candy cleanup failed: ring still open after 12 cancels")

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


class InnMacro:
    """Talk to the taught innkeeper and confirm the stay until HP is full.

    Success requires the leader's HP to reach max HP and the game to return
    to field mode. Pressing Confirm in someone else's dialog (a shop) could
    buy things, so this runs only on the exact taught tile and facing, and
    stops pressing as soon as the dialog does not behave like the inn.
    """

    POLL = 0.1
    MAX_CONFIRMS = 10

    def __init__(self, link, anchor, confirm="confirm", cancel="cancel", log=print,
                 sleep=time.sleep, clock=time.monotonic):
        self.link, self.anchor, self.log = link, anchor, log
        self.confirm, self.cancel = confirm, cancel
        self.sleep, self.clock = sleep, clock

    def _read(self):
        raw = self.link.fetch_wram(True)
        if raw is None:
            raise MacroFailed("physical takeover or game exit")
        return WRAM(raw)

    def _tile(self, wram):
        context = wram.tile_context()
        if not context.get("runtime_verified"):
            return None
        return (context["map_id"], context["actor"]["layer"], *context["actor_tile"])

    def ready(self, wram):
        if not isinstance(self.anchor, dict) or self.anchor.get("face") is None:
            return "no taught inn; run tools/som_teach_inn.py"
        if wram.game_mode() != "field":
            return "not in field mode"
        target = (self.anchor["map_id"], self.anchor["layer"], *self.anchor["target"])
        if self._tile(wram) != target:
            return "not on the taught innkeeper tile"
        hero = wram.character(0)
        if not hero["max_hp"] or hero["hp"] >= hero["max_hp"]:
            return "HP already full"
        return None

    def _leave_dialog(self):
        for _ in range(8):
            try:
                if self._read().game_mode() == "field":
                    return
            except (MacroFailed, OSError):
                return
            self.link.send_action(self.cancel)
            self.sleep(0.4)

    def run(self):
        start = self._read()
        why = self.ready(start)
        if why:
            raise MacroFailed(why)
        before = self._tile(start)
        # A short tap toward the counter turns the hero without moving.
        self.link.send_action(self.anchor["face"])
        self.sleep(0.3)
        if self._tile(self._read()) != before:
            raise MacroFailed("facing tap moved the hero; innkeeper is not on that side")
        self.link.send_action(self.confirm)
        deadline = self.clock() + 1.5
        while self._read().game_mode() == "field":
            if self.clock() >= deadline:
                raise MacroFailed("innkeeper did not start talking")
            self.sleep(self.POLL)
        self.log("[inn] innkeeper is talking")
        try:
            healed = False
            for _ in range(self.MAX_CONFIRMS):
                self.link.send_action(self.confirm)
                self.sleep(0.7)
                wram = self._read()
                hero = wram.character(0)
                healed = healed or (hero["max_hp"] and hero["hp"] >= hero["max_hp"])
                if healed and wram.game_mode() == "field":
                    return hero["hp"] - start.character(0)["hp"]
            if healed:
                self._leave_dialog()
                return self._read().character(0)["hp"] - start.character(0)["hp"]
            raise MacroFailed("no full HP after the inn dialog (enough GP? right NPC?)")
        except MacroFailed:
            self._leave_dialog()
            raise


# ---------------------------------------------------------------- gold / shop

MAX_ITEM_COUNT = 4  # Secret of Mana carries at most four of each item.
REPLAYABLE = {"confirm", "cancel", "open_ring", "menu_x", "prev_ring", "next_ring",
              "step_up", "step_down", "step_left", "step_right"}


def find_gold_candidates(wram, amount):
    """WRAM addresses holding amount as a little-endian 24- or 16-bit number."""
    found = []
    for address in range(0x7E0000, 0x7EFFFE):
        low = wram.u8(address) | wram.u8(address + 1) << 8
        if amount < 0x10000 and low == amount:
            found.append((address, 2))
        if low | wram.u8(address + 2) << 16 == amount:
            found.append((address, 3))
    return found


def narrow_gold(candidates, wram, amount):
    return [(address, width) for address, width in candidates
            if read_gold(wram, {"address": address, "width": width}) == amount]


def read_gold(wram, gold):
    if not isinstance(gold, dict):
        return None
    address, width = int(gold["address"]), int(gold.get("width", 3))
    value = wram.u8(address) | wram.u8(address + 1) << 8
    return value | wram.u8(address + 2) << 16 if width == 3 else value


def candy_count(wram):
    return next((item["count"] for item in wram.inventory()
                 if item["name"] == "candy" and item["verified"]), 0)


class ShopMacro(InnMacro):
    """Replay the player's recorded Candy purchase and verify it happened.

    Success needs one more Candy and less gold afterwards. Anything else
    cancels out of the dialog and disables shopping for the run. A replayed
    menu can still pick a different item if the shop's cursor remembers a
    different position, which the Candy check then reports as a failure.
    """

    GAP = 0.45

    def __init__(self, link, shop, gold, **kwargs):
        super().__init__(link, shop, **kwargs)
        self.gold = gold

    def ready(self, wram):
        buttons = self.anchor.get("buttons") if isinstance(self.anchor, dict) else None
        if not buttons or any(button not in REPLAYABLE for button in buttons):
            return "no replayable shop purchase taught; run tools/som_teach_shop.py"
        if wram.game_mode() != "field":
            return "not in field mode"
        target = (self.anchor["map_id"], self.anchor["layer"], *self.anchor["target"])
        if self._tile(wram) != target:
            return "not on the taught shopkeeper tile"
        gold = read_gold(wram, self.gold)
        if gold is None or gold < int(self.anchor.get("price", 0)):
            return "not enough gold"
        if candy_count(wram) >= MAX_ITEM_COUNT:
            return "already carrying four Candy"
        return None

    def run(self):
        start = self._read()
        why = self.ready(start)
        if why:
            raise MacroFailed(why)
        gold_before, candy_before = read_gold(start, self.gold), candy_count(start)
        before = self._tile(start)
        self.link.send_action(self.anchor["face"])
        self.sleep(0.3)
        if self._tile(self._read()) != before:
            raise MacroFailed("facing tap moved the hero; shopkeeper is not on that side")
        try:
            for index, button in enumerate(self.anchor["buttons"]):
                self.link.send_action(button)
                self.sleep(self.GAP)
                if index == 0 and self._read().game_mode() == "field":
                    raise MacroFailed("shopkeeper did not start talking")
            deadline = self.clock() + 2.0
            wram = self._read()
            while wram.game_mode() != "field" and self.clock() < deadline:
                self.sleep(self.POLL)
                wram = self._read()
            gold_after, candy_after = read_gold(wram, self.gold), candy_count(wram)
            if candy_after > candy_before and gold_after < gold_before:
                if wram.game_mode() != "field":
                    self._leave_dialog()
                return gold_before - gold_after
            raise MacroFailed(f"purchase not observed (Candy {candy_before}->{candy_after}, "
                              f"gold {gold_before}->{gold_after})")
        except MacroFailed:
            self._leave_dialog()
            raise

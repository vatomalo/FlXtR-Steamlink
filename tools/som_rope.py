"""Magic Rope: when the AI uses it to get out of a dungeon.

Player, 2026-11-23: "AI should use rope to get out". The proxy command
use_rope leaves by the way the game remembers ($0108), where the game allows
the rope ($011F bit 0x40); the rope is not used up. Two reasons to:

- circling: the last CIRCLE_CHANGES map changes stayed within CIRCLE_MAPS
  maps, took at least CIRCLE_S, and none of those maps was new (live
  2026-11-23: 314 > 276 > 314 > 269 for over half an hour);
- on the way to the inn or the shop from inside the dungeon.

Never with a monster in contact or a boss on the map, and not again at once:
not from the map the rope led to, COOLDOWN_S after a use, REFUSED_S after
one that did not move us. Pure logic; the bridge sends the command.
"""
import time

ROPE_ALLOWED = 0x7E011F          # bit 0x40
ROPE_TARGET = 0x7E0108           # transition number, 0 = none
BOSS_FIRST, BOSS_LAST = 0x57, 0x7F
CIRCLE_CHANGES = 8
CIRCLE_MAPS = 3
CIRCLE_S = 120.0
COOLDOWN_S = 600.0
REFUSED_S = 1800.0
CONFIRM_S = 8.0
ERRANDS = ("return_to_inn", "go_to_inn", "go_to_shop")


def rope_ready(wram):
    """The party has the rope and the game would let it be used here."""
    if not wram.u8(ROPE_ALLOWED) & 0x40 or not wram.u16(ROPE_TARGET):
        return False
    return any(item["name"] == "magic_rope" and item["verified"] for item in wram.inventory())


class RopeTactics:
    def __init__(self, clock=time.monotonic):
        self.clock = clock
        self.trail = []              # (time, map) of each map change
        self.known = set()           # maps seen before they entered the trail window
        self.fresh = set()           # maps first seen in this session's window
        self.sent = None             # (time, map) of a use not yet confirmed
        self.arrived = None          # the map the rope led to
        self.ready_at = 0.0

    def see_map(self, map_id):
        """Each decision. Returns a line to log, or None."""
        if map_id is None:
            return None
        now, line = self.clock(), None
        if self.sent is not None:
            started, origin = self.sent
            if map_id != origin:
                self.sent, self.arrived = None, map_id
                self.trail = []
                line = f"out of map {origin}: now on map {map_id}"
            elif now - started > CONFIRM_S:
                self.sent, self.ready_at = None, now + REFUSED_S
                line = f"still on map {origin}: not trying again for a while"
        if not self.trail or self.trail[-1][1] != map_id:
            if map_id in self.known:
                self.fresh.discard(map_id)
            else:
                self.known.add(map_id)
                self.fresh.add(map_id)
            self.trail.append((now, map_id))
            del self.trail[:-CIRCLE_CHANGES]
        if self.arrived is not None and map_id != self.arrived and self.sent is None and line is None:
            self.arrived = None      # walked on from the rope's landing
        return line

    def circling(self):
        if len(self.trail) < CIRCLE_CHANGES:
            return False
        maps = {m for _, m in self.trail}
        return (2 <= len(maps) <= CIRCLE_MAPS and not maps & self.fresh and
                self.trail[-1][0] - self.trail[0][0] >= CIRCLE_S)

    def decide(self, wram, map_id, intention, in_contact, enemies):
        """Why to use the rope now, or None."""
        if (self.sent is not None or in_contact or map_id is None or
                self.clock() < self.ready_at or map_id == self.arrived):
            return None
        if any(BOSS_FIRST <= (e.get("identity") or 0) <= BOSS_LAST for e in enemies):
            return None
        if self.circling():
            why = "circling maps " + ", ".join(str(m) for m in sorted({m for _, m in self.trail}))
        elif intention in ERRANDS:
            why = f"out of the dungeon ({intention})"
        else:
            return None
        return why if rope_ready(wram) else None

    def used(self, map_id):
        now = self.clock()
        self.sent, self.ready_at = (now, map_id), now + COOLDOWN_S

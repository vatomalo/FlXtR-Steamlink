"""Brief, verified pad-1 swaps for party spell assists.

The regular navigation planner still follows the boy. An assist switches to
the girl or sprite, casts with that member, then returns control to the boy.
Each Select press is checked against the game's pad binding before proceeding.
"""
from som_class_director import ATTACK_MAGIC, HEALERS


def lead_slot(observation):
    bindings = observation.get("pad_bindings") or []
    if not bindings:
        return None
    bits = bindings[0] & 7
    return (bits.bit_length() - 1) if bits in (1, 2, 4) else None


class PartyAssist:
    SWITCH_INTERVAL = 0.45
    CAST_SETTLE = 1.5
    COOLDOWN = 25.0

    def __init__(self):
        self.target = None
        self.command = None
        self.returning = False
        self.last_select = float("-inf")
        self.tries = 0
        self.ready_at = 0.0
        self.next_at = 0.0

    def _choose(self, observation, classes):
        party = observation.get("party") or []
        if len(party) < 3:
            return None
        bindings = observation.get("pad_bindings") or []
        if any(binding & 7 for binding in bindings[1:]):
            return None  # a second human controller owns a party member
        candidates = [(who, party[who], classes.get(who)) for who in (1, 2)
                      if party[who].get("availability_byte") == 1
                      and party[who].get("controller_binding") == 0
                      and (party[who].get("hp") or 0) > 0
                      and (party[who].get("mp") or 0) >= 2]
        fallen = any(p.get("max_hp") and p.get("hp") == 0 for p in party)
        if fallen:
            for who, _member, cls in candidates:
                if cls == 4:  # Cleric's Revivifier
                    return who, "cast_revive"
        wounded = any(p.get("max_hp") and 0 < (p.get("hp") or 0) <
                      p["max_hp"] * 0.40 for p in party)
        if wounded:
            for who, _member, cls in candidates:
                if cls in HEALERS:
                    return who, "cast_heal"
        enemies = [e for e in observation.get("enemy_slots") or []
                   if e.get("active") and not e.get("familiar")]
        if any((e.get("max_hp") or 0) >= 100 for e in enemies):
            for who, _member, cls in sorted(candidates,
                                             key=lambda c: -(c[1].get("mp") or 0)):
                if cls in ATTACK_MAGIC:
                    return who, "cast_attack"
        return None

    def step(self, observation, classes, now):
        """Return ('action'|'control'|'pending', command), or None."""
        lead = lead_slot(observation)
        if lead is None:
            return ("pending", None) if self.target is not None else None
        if self.target is None:
            if lead != 0:
                self.returning = True
            elif now >= self.next_at:
                chosen = self._choose(observation, classes)
                if chosen is not None:
                    self.target, self.command = chosen
                    self.returning = False
                    self.tries = 0
        goal = 0 if self.returning else self.target
        if goal is None:
            return None
        if lead == goal:
            self.tries = 0
            if self.returning:
                cooldown = 8.0 if self.command in ("cast_heal", "cast_revive") else self.COOLDOWN
                self.target = self.command = None
                self.returning = False
                self.next_at = now + cooldown
                return ("pending", None)
            command = self.command
            self.returning = True
            self.ready_at = now + self.CAST_SETTLE
            return ("control", command)
        if self.returning and now < self.ready_at:
            return ("pending", None)
        if now - self.last_select < self.SWITCH_INTERVAL:
            return ("pending", None)
        if self.tries >= 4:
            # Never let the normal boy planner drive a different member.
            self.target = self.command = None
            self.returning = lead != 0
            self.tries = 0
            self.next_at = now + self.COOLDOWN
            return ("pending", None)
        self.tries += 1
        self.last_select = now
        return ("action", "select")

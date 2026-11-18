"""Class techniques that replace magic, for the AI.

Bushido (samurai): no spells. Iaijutsu, the proxy's iai_3 / iai_4, is the
katana's charge level 3 or 4 straight away, one per 3 s (proxy cooldown
SOM_IAI_COOLDOWN_FRAMES). The AI draws it instead of a plain swing when the
planner is about to attack: level 4 against a tough enemy or a crowd,
level 3 otherwise.

Martial Arts (monk): the flying kick, the proxy's flykick (X/Triangle for
the player): the game's run of up to ~42 px, then the glove (the combo
finisher when a combo level is ready). The AI kicks at an enemy in line
ahead (within KICK_LINE_PX of the hero's facing line) between KICK_MIN_PX,
just past the glove's reach, and KICK_MAX_PX, the run plus the contact
distance, at most once per KICK_COOLDOWN_S.

Weapon Flow (weaponsmaster): no spells. Its third weapon (the bow,
som_class_director.THIRD_WEAPONS) joins the B/Y pair in som_weapon_tactics.
"""
import time

IAI_COOLDOWN_S = 3.0
TOUGH_HP = 200          # as som_class_director.TOUGH_HP
CROWD = 2
KICK_COOLDOWN_S = 2.5
KICK_MIN_PX = 26
KICK_MAX_PX = 60
KICK_LINE_PX = 12
_KICK_AXES = {"step_right": (1, 0), "step_left": (-1, 0), "step_down": (0, 1), "step_up": (0, -1)}


class IaiTactics:
    def __init__(self, clock=None):
        self.clock = clock or time.monotonic
        self.ready_at = 0.0

    def decide(self, planned_action, toughest_hp=0, enemies_near=0):
        """'iai_3' / 'iai_4' to send instead of the planned attack, or None."""
        if planned_action != "attack" or self.clock() < self.ready_at:
            return None
        self.ready_at = self.clock() + IAI_COOLDOWN_S
        if toughest_hp >= TOUGH_HP or enemies_near >= CROWD:
            return "iai_4"
        return "iai_3"

    def refused(self):
        """The proxy refused (busy or cooling down): try a bit later."""
        self.ready_at = self.clock() + 0.5


class FlyingKickTactics:
    def __init__(self, clock=None):
        self.clock = clock or time.monotonic
        self.ready_at = 0.0

    def decide(self, facing, hero_x, hero_y, foes):
        """'flykick' when an enemy (x, y) is in line ahead within the kick's
        distance and the kick is ready, else None."""
        axis = _KICK_AXES.get(facing)
        if axis is None or hero_x is None or hero_y is None or self.clock() < self.ready_at:
            return None
        for x, y in foes:
            ahead = (x - hero_x) * axis[0] + (y - hero_y) * axis[1]
            side = abs((x - hero_x) * axis[1]) + abs((y - hero_y) * axis[0])
            if KICK_MIN_PX <= ahead <= KICK_MAX_PX and side <= KICK_LINE_PX:
                self.ready_at = self.clock() + KICK_COOLDOWN_S
                return "flykick"
        return None

    def refused(self):
        self.ready_at = self.clock() + 0.5

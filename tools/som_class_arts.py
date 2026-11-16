"""Class techniques that replace magic, for the AI.

Bushido (samurai): no spells. Iaijutsu, the proxy's iai_3 / iai_4, is the
katana's charge level 3 or 4 straight away, one per 3 s (proxy cooldown
SOM_IAI_COOLDOWN_FRAMES). The AI draws it instead of a plain swing when the
planner is about to attack: level 4 against a tough enemy or a crowd,
level 3 otherwise.

Weapon Flow (weaponsmaster): no spells. Its third weapon (the bow,
som_class_director.THIRD_WEAPONS) joins the B/Y pair in som_weapon_tactics.
"""
import time

IAI_COOLDOWN_S = 3.0
TOUGH_HP = 200          # as som_class_director.TOUGH_HP
CROWD = 2


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

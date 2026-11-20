"""When the AI casts Kawarimi no Jutsu and Shunshin no Jutsu.

Neither is on the evade any more (A / "evade" is the plain dodge hop); the
proxy takes them as their own commands, "kawarimi" and "shunshin".

Kawarimi (the classes with a dodge): arming it lasts 3 s and the swap fires
when a monster touches or hits the hero, so it is armed right before a hit
is expected: the planner wants to guard, or HP is low, with an enemy
within KAWARIMI_PX. Not again until the window has closed.

Shunshin (ninja with a short-range weapon, som_weapon_ranges): a shadow clone on each side. Clones take
hits and explode on touch, so it is cast when an enemy is within SHUNSHIN_PX
in a fight, and again only after the 10 s clones are gone.

The "evade" combat gambit at 0 turns both off.
"""
import time

from som_weapon_ranges import is_short

DODGE_CLASSES = (0, 1, 2, 3, 9, 10, 11) # som_disc_has_dodge (3: Blink step)
NINJA = 2
KAWARIMI_PX = 40
KAWARIMI_COOLDOWN_S = 4.0               # 3 s armed + a second
KAWARIMI_LOW_HP = 0.50
SHUNSHIN_PX = 80
SHUNSHIN_COOLDOWN_S = 11.0              # clones last 10 s


class JutsuTactics:
    def __init__(self, clock=None):
        self.clock = clock or time.monotonic
        self.ready_at = {"kawarimi": 0.0, "shunshin": 0.0}

    def decide(self, discipline, held, distance, hp_fraction, guarding, evade_weight=1,
               kawarimi_px=KAWARIMI_PX, kawarimi_hp=KAWARIMI_LOW_HP, shunshin_px=SHUNSHIN_PX):
        """(command, reason) to send now, or None.

        The thresholds default to the hand-set constants; the bridge passes
        the values som_trials has learned per enemy type."""
        if discipline is None or distance is None or not evade_weight:
            return None
        now = self.clock()
        if (discipline == NINJA and is_short(held) and distance <= shunshin_px and
                now >= self.ready_at["shunshin"]):
            self.ready_at["shunshin"] = now + SHUNSHIN_COOLDOWN_S
            return "shunshin", f"enemy {distance} px away: shadow clones on each side"
        if (discipline in DODGE_CLASSES and distance <= kawarimi_px and
                (guarding or hp_fraction < kawarimi_hp) and
                now >= self.ready_at["kawarimi"]):
            self.ready_at["kawarimi"] = now + KAWARIMI_COOLDOWN_S
            why = ("a hit is coming" if guarding else
                   "armed early" if kawarimi_hp > 1 else f"HP {hp_fraction:.0%}")
            return "kawarimi", f"enemy {distance} px away, {why}: substitution armed"
        return None

    def refused(self, command):
        """The proxy refused (busy, no room): try again on the next look."""
        self.ready_at[command] = 0.0

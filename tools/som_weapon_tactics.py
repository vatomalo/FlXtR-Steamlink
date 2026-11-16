"""Alternate between the class's two weapons, and say why.

Each discipline carries two weapons (B primary, Y secondary; WEAPONS in
som_class_director). The reach numbers are the planner's
(som_planner.WEAPON_REACH_PX, from som-decomp weapons-ranged.md): melee
weapons hit within ~28 px, the whip 18-80 px, bow/javelin/boomerang to ~70.

Rules, first match wins:
1. Too close for the whip (it misses under ~18 px): the other weapon.
2. Low HP (under 40 %) and an enemy near: the longer reach, to fight from
   outside the enemy's bite.
3. Enemy beyond melee reach but inside the other weapon's: the longer
   reach, so the hero hits without walking into it.
4. Enemy at melee range: the stronger weapon here, the higher weapon level
   (more charge levels), else the primary.
5. No enemy near: train the weaker weapon when it lags two levels behind,
   else ready the primary.
Switches wait `cooldown` seconds (SWITCH_COOLDOWN_S by default; the bridge
scales it with the "Switch weapons" combat gambit, 0 = never).
Pure logic; the bridge sends equip_<weapon> and prints the reason.
"""
from som_planner import DEFAULT_STRIKE_AXIAL_PX, WEAPON_MIN_PX, WEAPON_REACH_PX

SWITCH_COOLDOWN_S = 4.0
LOW_HP = 0.40
NEAR_PX = 160          # an enemy this close counts as a fight
TRAIN_GAP = 2


def reach(weapon):
    return WEAPON_REACH_PX.get(weapon, DEFAULT_STRIKE_AXIAL_PX)


def choose(primary, secondary, held, distance, hp_fraction, levels):
    """(weapon, reason) to hold now; distance None = no enemy near."""
    levels = levels or {}
    pair = [w for w in (primary, secondary) if w]
    if len(pair) < 2 or pair[0] == pair[1]:
        return (pair[0], "only one weapon") if pair else (held, "no class weapons")
    longer = max(pair, key=reach)
    shorter = min(pair, key=reach)
    if distance is not None and distance <= NEAR_PX:
        if held in WEAPON_MIN_PX and distance <= WEAPON_MIN_PX[held] + 4:
            other = shorter if held == longer else longer
            return other, f"enemy {distance} px away is too close for the {held}"
        if hp_fraction < LOW_HP and reach(longer) > reach(shorter):
            return longer, (f"HP {hp_fraction:.0%}: the {longer} hits from "
                            f"{reach(longer)} px, outside the enemy's reach")
        if reach(shorter) < distance <= reach(longer):
            return longer, (f"enemy {distance} px away: the {longer} reaches "
                            f"{reach(longer)} px, the {shorter} only {reach(shorter)}")
        stronger = max(pair, key=lambda w: (levels.get(w, 0), w == primary))
        return stronger, (f"close fight: the {stronger} is level {levels.get(stronger, 0)}"
                          + ("" if levels.get(pair[0], 0) == levels.get(pair[1], 0)
                             else f" (the other {levels.get(pair[0] if stronger == pair[1] else pair[1], 0)})"))
    weaker = min(pair, key=lambda w: (levels.get(w, 0), w != primary))
    stronger = pair[0] if weaker == pair[1] else pair[1]
    if levels.get(stronger, 0) - levels.get(weaker, 0) >= TRAIN_GAP:
        return weaker, (f"no enemy near: training the {weaker} (level "
                        f"{levels.get(weaker, 0)} vs {levels.get(stronger, 0)})")
    return primary, f"no enemy near: the {primary} ready"


class WeaponTactics:
    def __init__(self, clock=None):
        import time
        self.clock = clock or time.monotonic
        self.switched_at = None
        self.last_reason = None
        self.cooldown = SWITCH_COOLDOWN_S     # the bridge sets it from the gambit; None = never

    def decide(self, primary, secondary, held, distance, hp_fraction, levels, busy=False):
        """(weapon, reason) to switch to now, or None to keep the one in hand."""
        weapon, reason = choose(primary, secondary, held, distance, hp_fraction, levels)
        if weapon == held or weapon is None or busy or self.cooldown is None:
            return None
        now = self.clock()
        if self.switched_at is not None and now - self.switched_at < self.cooldown:
            return None
        self.switched_at, self.last_reason = now, reason
        return weapon, reason

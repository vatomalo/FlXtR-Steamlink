"""The eight weapons grouped by range: one table for planner, tactics, jutsu.

Reach is the forward reach of a normal attack (som-decomp weapons-ranged.md
2.4 and the projectile tables: whip box 26-86 px ahead and nothing under
~16 px; bow, javelin and boomerang stage 0 hit 14-70 px; spear a little
longer than the sword), kept inside the measured hits.

SHORT: hit what is in front of the hero; walk up to the enemy.
LONG: hit from a distance; line the shot up from where the hero stands and
do not walk into melee. The proxy has the same split by weapon id
(src/som_disciplines.h som_weapon_long_range).
"""
from dataclasses import dataclass

SHORT, LONG = "short", "long"
DEFAULT_REACH_PX = 28       # unknown weapon: the conservative sword envelope


@dataclass(frozen=True)
class WeaponRange:
    name: str
    weapon_id: int          # 7E:E1E4 / proxy som_weapon_types order
    reach_px: int
    kind: str               # SHORT or LONG
    attack: str             # "melee", "lash" or "projectile"
    min_px: int = 0         # misses anything closer than this

    @property
    def long(self):
        return self.kind == LONG


WEAPONS = (
    WeaponRange("glove", 0, 24, SHORT, "melee"),
    WeaponRange("sword", 1, 28, SHORT, "melee"),
    WeaponRange("axe", 2, 28, SHORT, "melee"),
    WeaponRange("spear", 3, 34, SHORT, "melee"),
    WeaponRange("whip", 4, 80, LONG, "lash", min_px=18),
    WeaponRange("bow", 5, 70, LONG, "projectile"),
    WeaponRange("boomerang", 6, 64, LONG, "projectile"),
    WeaponRange("javelin", 7, 70, LONG, "projectile"),
)
BY_NAME = {w.name: w for w in WEAPONS}
SHORT_RANGE = tuple(w.name for w in WEAPONS if w.kind == SHORT)
LONG_RANGE = tuple(w.name for w in WEAPONS if w.kind == LONG)


def weapon(name):
    """The WeaponRange for a weapon name, or None when unknown."""
    return BY_NAME.get(name)


def reach(name):
    w = BY_NAME.get(name)
    return w.reach_px if w else DEFAULT_REACH_PX


def min_reach(name):
    w = BY_NAME.get(name)
    return w.min_px if w else 0


def is_long(name):
    return name in LONG_RANGE


def is_short(name):
    return name in SHORT_RANGE

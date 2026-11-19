"""Class weapon styles: the Python copy of src/som_weapon_styles.h.

Each class's B weapon comes in three styles (fast, balanced, heavy), e.g.
Bushido's Tanto, Katana and Dai Katana. Against the plain weapon a style
changes power_pct (attack, 100 = plain), speed_pct (how fast the attack
cooldown runs down, 150 = half again as fast) and reach_bonus (px past the
game's own reach for a "long cut", melee weapons only). Weapon ids follow
som_weapon_types: 0 glove, 1 sword, 2 axe, 3 spear, 4 whip, 5 bow,
6 boomerang, 7 javelin. tools/test_som_weapon_styles.py keeps this table
equal to the C one.
"""
from dataclasses import dataclass

WEAPON_TYPES = ("glove", "sword", "axe", "spear", "whip", "bow", "boomerang", "javelin")
CLASSES = ("Swordsmanship", "Martial Arts", "Ninjutsu", "Magic", "Divine Arts",
           "Dark Arts", "Summoning", "Support & Dance", "Weapon Flow",
           "Roguecraft", "Bushido", "Archery")
STYLES_PER_CLASS = 3
FAST, BALANCED, HEAVY = range(STYLES_PER_CLASS)
DEFAULT_STYLE = BALANCED
MAX_LEVEL = 8


@dataclass(frozen=True)
class WeaponStyle:
    name: str
    weapon_id: int
    reach_bonus: int
    speed_pct: int
    power_pct: int

    @property
    def weapon(self):
        return WEAPON_TYPES[self.weapon_id]


_S = WeaponStyle
# Per class, in CLASSES order: fast, balanced, heavy.
STYLES = (
    _S("Rapier", 1, 0, 150, 75), _S("Longsword", 1, 0, 100, 100), _S("Claymore", 1, 16, 70, 135),
    _S("Hand Wraps", 0, 0, 160, 70), _S("Cestus", 0, 0, 100, 100), _S("Tiger Claws", 0, 10, 75, 130),
    _S("Kunai", 1, 0, 165, 65), _S("Ninjato", 1, 0, 100, 100), _S("Kusarigama", 1, 22, 70, 120),
    _S("Switch", 4, 0, 140, 80), _S("Ember Lash", 4, 0, 100, 100), _S("Spellcord", 4, 0, 75, 130),
    _S("Short Spear", 3, 0, 145, 80), _S("Holy Lance", 3, 0, 100, 100), _S("Halberd", 3, 14, 70, 135),
    _S("Cat-o'-Nine", 4, 0, 150, 75), _S("Scourge", 4, 0, 100, 100), _S("Bone Chain", 4, 0, 70, 135),
    _S("Talisman Ring", 6, 0, 145, 80), _S("Spirit Disc", 6, 0, 100, 100), _S("Moon Glaive", 6, 0, 70, 135),
    _S("War Fans", 4, 0, 155, 70), _S("Ribbon", 4, 0, 100, 100), _S("Long Sash", 4, 0, 75, 125),
    _S("Gladius", 1, 0, 145, 80), _S("Arming Sword", 1, 0, 100, 100), _S("Bastard Sword", 1, 14, 75, 130),
    _S("Dagger", 1, 0, 165, 65), _S("Cutlass", 1, 0, 100, 100), _S("Estoc", 1, 12, 85, 115),
    _S("Tanto", 1, 0, 160, 70), _S("Katana", 1, 0, 100, 100), _S("Dai Katana", 1, 20, 70, 135),
    _S("Shortbow", 5, 0, 150, 75), _S("Recurve Bow", 5, 0, 100, 100), _S("Longbow", 5, 0, 70, 135),
)
del _S


def style(klass, which=DEFAULT_STYLE):
    """The style `which` of class `klass`, or None out of range."""
    if not (0 <= klass < len(CLASSES) and 0 <= which < STYLES_PER_CLASS):
        return None
    return STYLES[klass * STYLES_PER_CLASS + which]


def class_styles(klass):
    """(fast, balanced, heavy) of a class, or () out of range."""
    if not 0 <= klass < len(CLASSES):
        return ()
    return STYLES[klass * STYLES_PER_CLASS:(klass + 1) * STYLES_PER_CLASS]


def attack(st, level, atk_low, atk_high):
    """som_style_attack: tier-0 attack at level 1, top tier at MAX_LEVEL."""
    if st is None:
        return atk_low
    level = min(max(level, 1), MAX_LEVEL)
    atk_high = max(atk_high, atk_low)
    plain = atk_low + (atk_high - atk_low) * (level - 1) // (MAX_LEVEL - 1)
    return plain * st.power_pct // 100 or 1

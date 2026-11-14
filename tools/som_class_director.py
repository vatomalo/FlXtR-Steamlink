"""Pick the discipline (class) that fits the hero best while the AI plays.

The proxy owns the classes (src/som_disciplines.h, weapons in
som_ai_proxy.c som_disc_weapons, spells in src/som_magic.h); this module
only decides which one to ask for with the `class_<n>` control command.

Fit = weapon levels of the class's two weapons (B counts double) + how well
the class suits the hero's build (som_build) + spells only when the hero
has the MP to cast them. need_cut can restrict the choice to sword
classes, but the bridge does not use it: for grass it keeps the class and
only pulls the sword for the cut (equip_sword). Switches wait for a calm
moment and a cooldown so the hero does not flip classes every step.
Pure logic; the bridge sends the command.
"""

NAMES = ("Swordsmanship", "Martial Arts", "Ninjutsu", "Magic", "Divine Arts",
         "Dark Arts", "Summoning", "Support & Dance", "Weapon Flow",
         "Roguecraft", "Bushido", "Archery")

# (B weapon, Y weapon) per class, as som_disc_weapons in som_ai_proxy.c.
WEAPONS = (("sword", "axe"), ("glove", "spear"), ("sword", "boomerang"),
           ("whip", "boomerang"), ("spear", "whip"), ("whip", "javelin"),
           ("boomerang", "whip"), ("whip", "glove"), ("sword", "spear"),
           ("sword", "bow"), ("sword", "bow"), ("bow", "javelin"))

SWORD_CLASSES = tuple(i for i, (b, _) in enumerate(WEAPONS) if b == "sword")

# Build -> class bonus (som_build.classify names).
BUILD_BONUS = {
    "brawler": {0: 3, 10: 3, 1: 2, 8: 1},
    "skirmisher": {2: 3, 9: 3, 11: 2, 1: 1},
    "guardian": {8: 3, 0: 2, 4: 1},
    "caster": {3: 3, 4: 2, 5: 2, 6: 2},
    "balanced": {8: 2, 0: 1},
}
# Classes whose spells are worth MP (attack or healing magic).
MAGIC_BONUS = {3: 3, 4: 2, 5: 2, 6: 1, 2: 1, 7: 1}
HEALERS = (4, 7)                     # Cure Water in their spell list
MIN_SPELL_MP = 4                     # two casts of the cheap spells

SWITCH_MARGIN = 2                    # a better class must win by this much
COOLDOWN_S = 60.0


def score(index, build, levels, max_mp):
    b, y = WEAPONS[index]
    levels = levels or {}
    value = 2 * int(levels.get(b, 0) or 0) + int(levels.get(y, 0) or 0)
    value += BUILD_BONUS.get(build, BUILD_BONUS["balanced"]).get(index, 0)
    if (max_mp or 0) >= MIN_SPELL_MP:
        value += MAGIC_BONUS.get(index, 0)
    return value


class ClassDirector:
    def __init__(self, clock=None):
        import time
        self.clock = clock or time.monotonic
        self.current = None          # what this director last asked for
        self.switched_at = None

    def choose(self, build, levels, max_mp, need_cut=False, in_combat=False):
        """(class index, reason) to switch to now, or None to stay."""
        candidates = SWORD_CLASSES if need_cut else range(len(NAMES))
        best = max(candidates, key=lambda i: (score(i, build, levels, max_mp), -i))
        if best == self.current:
            return None
        if self.current is not None:
            if in_combat:
                return None
            sword_needed = need_cut and self.current not in SWORD_CLASSES
            if not sword_needed:
                if (self.switched_at is not None and
                        self.clock() - self.switched_at < COOLDOWN_S):
                    return None
                if need_cut:         # already holds a sword: keep it
                    return None
                if (score(best, build, levels, max_mp) <
                        score(self.current, build, levels, max_mp) + SWITCH_MARGIN):
                    return None
        reason = ("grass ahead needs a sword" if need_cut else
                  f"fits a {build} hero best (score {score(best, build, levels, max_mp)})")
        return best, reason

    def switched(self, index):
        self.current = index
        self.switched_at = self.clock()

"""Pick the discipline (class) that fits the moment while the AI plays.

The proxy owns the classes (src/som_disciplines.h, weapons in
som_ai_proxy.c som_disc_weapons, spells in src/som_magic.h); this module
only decides which one to ask for with the `class_<n>` control command.

Each class has advantages (ADVANTAGES) that count only when the situation
calls for them: healing when hurt, range when the enemy is far, area magic
and clones against a crowd, charged blows against a tough monster, fast
fists against one close enemy. Weapon levels (B counts double) and the
hero's build (som_build) are the base; spells count only with MP. The
best class wins when it beats the current one by SWITCH_MARGIN; switches
wait for a moment without an enemy in contact and for `cooldown` seconds,
which the bridge sets from the "Switch classes" combat gambit (0 = never).
need_cut can restrict the choice to sword classes (the bridge borrows the
sword for grass instead). Pure logic; the bridge sends the command.
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
DODGE_CLASSES = (0, 1, 2, 9, 10, 11)          # som_disc_has_dodge
ATTACK_MAGIC = (2, 3, 4, 5, 6, 7, 9, 11)      # classes with a hostile spell (som_magic.h)
HEALERS = (4, 7)                              # Cure Water in their spell list

# Build -> class bonus (som_build.classify names).
BUILD_BONUS = {
    "brawler": {0: 3, 10: 3, 1: 2, 8: 1},
    "skirmisher": {2: 3, 9: 3, 11: 2, 1: 1},
    "guardian": {8: 3, 0: 2, 4: 1},
    "caster": {3: 3, 4: 2, 5: 2, 6: 2},
    "balanced": {8: 2, 0: 1},
}
MIN_SPELL_MP = 4                     # two casts of the cheap spells

# (situation test, class, bonus, needs MP, why)
ADVANTAGES = (
    ("hurt", 4, 6, True, "Cure Water heals us"),
    ("hurt", 7, 3, True, "buffs and Cure Water"),
    ("crowd", 3, 4, True, "spells hit a crowd from afar"),
    ("crowd", 2, 3, False, "shadow clones and evasion against a crowd"),
    ("crowd", 7, 2, True, "hexes slow the crowd"),
    ("far", 11, 4, False, "arrows reach the distant enemy"),
    ("far", 3, 2, True, "spells reach the distant enemy"),
    ("tough", 10, 4, False, "charged blows break a tough enemy"),
    ("tough", 0, 2, False, "a strong blade for a tough enemy"),
    ("tough", 5, 3, True, "draining a tough enemy"),
    ("tough", 9, 2, True, "Sleep Flower and Speed Down for a tough enemy"),
    ("duel", 1, 3, False, "fast fists for one close enemy"),
    ("calm", 8, 1, False, "sabers ready while exploring"),
)
TOUGH_HP = 200                       # enemy max HP that counts as tough

LINES = {
    0: "Steel is my answer!", 1: "Fists of the falls!", 2: "Shadows, hide me!",
    3: "Mana, burn bright!", 4: "Light, mend us!", 5: "Darkness, feed on them!",
    6: "Spirits, answer my call!", 7: "Dance with me!", 8: "Every blade, one flow!",
    9: "They won't see me coming.", 10: "One cut, one life.", 11: "Steady... loose!",
}

SWITCH_MARGIN = 1
CONTACT_PX = 40                      # no switching with an enemy this close
DEFAULT_COOLDOWN_S = 50.0


def situation_tags(hp_fraction=1.0, enemies=0, nearest=None, toughest=0):
    tags = set()
    if hp_fraction < 0.5:
        tags.add("hurt")
    if enemies >= 2:
        tags.add("crowd")
    if nearest is not None and nearest > 64:
        tags.add("far")
    if toughest >= TOUGH_HP:
        tags.add("tough")
    if enemies == 1 and nearest is not None and nearest <= 48:
        tags.add("duel")
    if not enemies:
        tags.add("calm")
    return tags


def score(index, build, levels, max_mp, tags=frozenset()):
    """(score, strongest advantage reason or None) of a class right now."""
    b, y = WEAPONS[index]
    levels = levels or {}
    value = 2 * int(levels.get(b, 0) or 0) + int(levels.get(y, 0) or 0)
    value += BUILD_BONUS.get(build, BUILD_BONUS["balanced"]).get(index, 0)
    magic = (max_mp or 0) >= MIN_SPELL_MP
    best = (0, None)
    for tag, klass, bonus, needs_mp, why in ADVANTAGES:
        if klass == index and tag in tags and (magic or not needs_mp):
            value += bonus
            best = max(best, (bonus, why), key=lambda item: item[0])
    return value, best[1]


class ClassDirector:
    def __init__(self, clock=None):
        import time
        self.clock = clock or time.monotonic
        self.current = None          # what this director last asked for
        self.switched_at = None
        self.cooldown = DEFAULT_COOLDOWN_S   # None = never switch (gambit 0)

    def choose(self, build, levels, max_mp, need_cut=False, in_combat=False, **situation):
        """(class index, reason) to switch to now, or None to stay.

        situation: hp_fraction, enemies, nearest (px or None), toughest (max HP).
        in_combat = an enemy in contact (no switching then)."""
        tags = situation_tags(**situation)
        candidates = SWORD_CLASSES if need_cut else range(len(NAMES))
        ranked = sorted(candidates, key=lambda i: (score(i, build, levels, max_mp, tags)[0], -i),
                        reverse=True)
        best = ranked[0]
        if best == self.current:
            return None
        best_value, why = score(best, build, levels, max_mp, tags)
        if self.current is not None:
            if self.cooldown is None or in_combat:
                return None
            sword_needed = need_cut and self.current not in SWORD_CLASSES
            if not sword_needed:
                if (self.switched_at is not None and
                        self.clock() - self.switched_at < self.cooldown):
                    return None
                if need_cut:         # already holds a sword: keep it
                    return None
                if best_value < score(self.current, build, levels, max_mp, tags)[0] + SWITCH_MARGIN:
                    return None
        reason = ("grass ahead needs a sword" if need_cut else
                  why or f"fits a {build} hero best (score {best_value})")
        return best, reason

    def switched(self, index):
        self.current = index
        self.switched_at = self.clock()

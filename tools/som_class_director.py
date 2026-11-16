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

Wanted build (set_plan): the few classes the hero means to master. The AI
picks them (plan_classes: stat fit plus each class's learned record of
damage dealt against HP lost, untried classes first); the player can fix
them instead (--wanted-classes). Wanted classes get PLAN_BONUS in fights,
and with no enemy around the hero rotates into the wanted class whose
weapons are least trained, so the plan's weapons level up together.
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

# Weapon Flow's third weapon (X; som_disc_third_weapon in som_disciplines.h).
THIRD_WEAPONS = {8: "bow"}
BUSHIDO = 10                                  # X = Iaijutsu, no spells
SWORD_CLASSES = tuple(i for i, (b, _) in enumerate(WEAPONS) if b == "sword")
DODGE_CLASSES = (0, 1, 2, 9, 10, 11)          # som_disc_has_dodge
ATTACK_MAGIC = (2, 3, 4, 5, 6, 7, 9, 11)      # classes with a hostile spell (som_magic.h)
# Classes whose X spells are self buffs worth casting as a fight starts:
# Swordsmanship's sabers, Martial Arts' body buffs, Support & Dance.
BUFF_CLASSES = (0, 1, 7)
NO_MAGIC = (8, 10)                            # Weapon Flow, Bushido: X is not a spell
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
    ("far", 8, 1, False, "the bow for range, sword and spear up close"),
    ("crowd", 10, 2, False, "Iaijutsu cuts through a crowd"),
)
TOUGH_HP = 200                       # enemy max HP that counts as tough

LINES = {
    0: "Steel is my answer!", 1: "Fists of the falls!", 2: "Shadows, hide me!",
    3: "Mana, burn bright!", 4: "Light, mend us!", 5: "Darkness, feed on them!",
    6: "Spirits, answer my call!", 7: "Dance with me!", 8: "Every blade, one flow!",
    9: "They won't see me coming.", 10: "One cut, one life.", 11: "Steady... loose!",
}

# Hysteresis: situations change every fight, so a new class must beat the
# current one clearly and the current one is kept at least MIN_HOLD_S
# (it flipped between five classes every 1-3 minutes on 2026-11-16).
SWITCH_MARGIN = 3
MIN_HOLD_S = 90.0
CONTACT_PX = 40                      # no switching with an enemy this close
DEFAULT_COOLDOWN_S = 50.0


PLAN_SIZE = 3                        # classes in the wanted build
PLAN_BONUS = 2                       # fight score bonus for a wanted class
TRAIN_GAP = 1                        # level gap before rotating to train
PLAN_MIN_SAMPLES = 60                # decisions before a class's record counts
PLAN_CURIOSITY = 3                   # an untried class's stand-in record value
PLAN_RECORD_CAP = 6                  # learned record clamp (per-decision score)
HP_WEIGHT = 2.0                      # HP lost counts double (as som_tactics)


def parse_classes(text):
    """Class indices from "Bushido, archery, 8" (names, prefixes or numbers)."""
    picked = []
    for part in (text or "").split(","):
        part = part.strip().lower()
        if not part:
            continue
        if part.isdigit() and int(part) < len(NAMES):
            index = int(part)
        else:
            matches = [i for i, name in enumerate(NAMES) if name.lower().startswith(part)]
            if len(matches) != 1:
                raise ValueError(f"unknown class {part!r} (one of: {', '.join(NAMES)})")
            index = matches[0]
        if index not in picked:
            picked.append(index)
    return picked


def record_score(samples, dealt, taken):
    """Learned value of a class per decision, or None while too few samples."""
    if samples < PLAN_MIN_SAMPLES:
        return None
    value = (dealt - HP_WEIGHT * taken) / samples
    return max(-PLAN_RECORD_CAP, min(PLAN_RECORD_CAP, value))


def plan_classes(build, record=None, max_mp=0, size=PLAN_SIZE):
    """(wanted classes, why): the AI's own build plan.

    Stat fit (the build bonus, doubled) plus how each class has actually
    fought (record {class: (samples, dealt, taken)}); classes not yet tried
    enough get PLAN_CURIOSITY so the hero gives them a fair go. Spell-only
    strengths need MP, so without MP the healers drop out.

    Slots are filled one by one; equal values go to the class fights have
    already picked most (samples), then the one with more situation
    strengths it can use, then the one bringing weapons the plan lacks
    (list order decided ties before: Martial Arts won by being second).
    """
    record = record or {}
    bonus = BUILD_BONUS.get(build, BUILD_BONUS["balanced"])
    magic = (max_mp or 0) >= MIN_SPELL_MP
    options = {}
    for index in range(len(NAMES)):
        if index in HEALERS and not magic:
            continue
        samples = record.get(index, (0, 0, 0))[0]
        learned = record_score(*record.get(index, (0, 0, 0)))
        value = 2 * bonus.get(index, 0) + (PLAN_CURIOSITY if learned is None else learned)
        strengths = len({tag for tag, klass, _, needs_mp, _ in ADVANTAGES
                         if klass == index and (magic or not needs_mp)})
        options[index] = (value, samples, strengths, learned)
    chosen, covered = [], set()
    while options and len(chosen) < size:
        def rank(index):
            value, samples, strengths, _ = options[index]
            weapons = set(WEAPONS[index]) | ({THIRD_WEAPONS[index]} if index in THIRD_WEAPONS
                                            else set())
            return (value, samples, strengths, len(weapons - covered), -index)
        pick = max(options, key=rank)
        chosen.append((pick, options.pop(pick)[3]))
        covered |= set(WEAPONS[pick]) | ({THIRD_WEAPONS[pick]} if pick in THIRD_WEAPONS
                                         else set())
    parts = [f"{NAMES[index]} {'untried' if learned is None else f'{learned:+.1f}'}"
             for index, learned in chosen]
    return sorted(index for index, _ in chosen), f"{build} build; " + ", ".join(parts)


def training_need(index, levels):
    """How trained a class's weapons are (lower = needs training more)."""
    levels = levels or {}
    b, y = WEAPONS[index]
    total = int(levels.get(b, 0) or 0) + int(levels.get(y, 0) or 0)
    third = THIRD_WEAPONS.get(index)
    if third:
        total = (total * 2 + 2 * int(levels.get(third, 0) or 0)) / 3
    return total


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
        self.wanted = ()             # the wanted build (class indices)
        self.wanted_fixed = False    # set by the player: the AI keeps it

    def set_plan(self, classes, fixed=False):
        """Adopt a wanted build; returns True when it changed."""
        classes = tuple(sorted(set(classes)))
        changed = classes != self.wanted
        self.wanted, self.wanted_fixed = classes, fixed
        return changed

    def _score(self, index, build, levels, max_mp, tags):
        value, why = score(index, build, levels, max_mp, tags)
        if index in self.wanted:
            value += PLAN_BONUS
        return value, why

    def _training_pick(self, levels, need_cut):
        """(class, reason) to train while calm, or None to stay as is."""
        wanted = [i for i in self.wanted if not need_cut or i in SWORD_CLASSES]
        if not wanted:
            return None
        pick = min(wanted, key=lambda i: (training_need(i, levels), i))
        if (self.current in wanted and
                training_need(self.current, levels) < training_need(pick, levels) + TRAIN_GAP):
            return None
        if pick == self.current:
            return None
        b, y = WEAPONS[pick]
        weakest = min((b, y), key=lambda w: int((levels or {}).get(w, 0) or 0))
        plan = ", ".join(NAMES[i] for i in self.wanted)
        return pick, f"training the {weakest} for the plan ({plan})"

    def choose(self, build, levels, max_mp, need_cut=False, in_combat=False, **situation):
        """(class index, reason) to switch to now, or None to stay.

        situation: hp_fraction, enemies, nearest (px or None), toughest (max HP).
        in_combat = an enemy in contact (no switching then)."""
        tags = situation_tags(**situation)
        training = None
        if "calm" in tags and self.wanted:
            training = self._training_pick(levels, need_cut)
            if training is None and (self.current in self.wanted or
                                     (need_cut and self.current in SWORD_CLASSES)):
                return None
        if training is not None:
            best, why = training
            best_value = None
        else:
            candidates = SWORD_CLASSES if need_cut else range(len(NAMES))
            ranked = sorted(candidates,
                            key=lambda i: (self._score(i, build, levels, max_mp, tags)[0], -i),
                            reverse=True)
            best = ranked[0]
            best_value, why = self._score(best, build, levels, max_mp, tags)
        if best == self.current:
            return None
        if self.current is not None:
            if self.cooldown is None or in_combat:
                return None
            sword_needed = need_cut and self.current not in SWORD_CLASSES
            if not sword_needed:
                if (self.switched_at is not None and
                        self.clock() - self.switched_at < max(self.cooldown, MIN_HOLD_S)):
                    return None
                if need_cut:         # already holds a sword: keep it
                    return None
                if (best_value is not None and best_value <
                        self._score(self.current, build, levels, max_mp, tags)[0] + SWITCH_MARGIN):
                    return None
        reason = ("grass ahead needs a sword" if need_cut else
                  why or f"fits a {build} hero best (score {best_value})")
        return best, reason

    def switched(self, index):
        self.current = index
        self.switched_at = self.clock()


class ClassRecord:
    """Damage dealt and HP lost per decision under each class (sqlite).

    Same sampling as som_tactics.TacticLearner: each decision next to an
    enemy opens a sample settled on the next one.
    """

    def __init__(self, connection):
        self.db = connection
        self.pending = None
        with connection:
            connection.execute(
                "CREATE TABLE IF NOT EXISTS class_stats (class INTEGER PRIMARY KEY, "
                "samples INTEGER NOT NULL, dealt INTEGER NOT NULL, taken INTEGER NOT NULL)")

    def stats(self):
        return {int(k): (n, d, t) for k, n, d, t in self.db.execute(
            "SELECT class,samples,dealt,taken FROM class_stats")}

    def record(self, observation, klass):
        from som_tactics import _nearest
        hero = ((observation or {}).get("party") or [{}])[0] or {}
        hp, max_hp = hero.get("hp"), hero.get("max_hp") or 0
        map_id = ((observation or {}).get("tile_context") or {}).get("map_id")
        if self.pending is not None and isinstance(hp, int):
            before_class, slot, identity, hero_before, enemy_before, pending_map = self.pending
            lost = hero_before - hp
            if hp > 0 and 0 <= lost and (not max_hp or lost < max_hp) and map_id == pending_map:
                enemy = next((e for e in (observation or {}).get("enemy_slots", [])
                              if isinstance(e, dict) and e.get("slot") == slot), None)
                if (enemy is not None and enemy.get("active") and
                        enemy.get("identity") == identity and isinstance(enemy.get("hp"), int)):
                    dealt = max(0, enemy_before - enemy["hp"])
                else:
                    dealt = enemy_before          # gone: defeated (or despawned)
                with self.db:
                    self.db.execute(
                        "INSERT INTO class_stats VALUES(?,1,?,?) ON CONFLICT(class) DO UPDATE "
                        "SET samples=samples+1,dealt=dealt+excluded.dealt,"
                        "taken=taken+excluded.taken", (before_class, dealt, lost))
        self.pending = None
        enemy = _nearest(observation)
        if (klass is not None and enemy is not None and isinstance(hp, int) and hp > 0 and
                isinstance(enemy.get("hp"), int)):
            self.pending = (klass, enemy.get("slot"), enemy.get("identity"), hp,
                            enemy["hp"], map_id)

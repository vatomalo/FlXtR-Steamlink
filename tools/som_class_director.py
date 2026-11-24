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
import itertools


NAMES = ("Swordsmanship", "Martial Arts", "Ninjutsu", "Magic", "Divine Arts",
         "Dark Arts", "Summoning", "Support & Dance", "Weapon Flow",
         "Roguecraft", "Bushido", "Archery",
         "Vampire Hunting", "Necromancy", "Artifice", "Psionics")

# (B weapon, Y weapon) per class, as som_disc_weapons in som_ai_proxy.c.
# Casters (Magic .. Support & Dance) hold no weapon: bare hands, B/Y cast.
WEAPONS = (("sword", "axe"), ("glove", "spear"), ("sword", "boomerang"),
           ("glove", "glove"), ("glove", "glove"), ("glove", "glove"),
           ("glove", "glove"), ("glove", "glove"), ("sword", "spear"),
           ("sword", "bow"), ("sword", "bow"), ("bow", "javelin"),
           ("whip", "boomerang"), ("glove", "glove"), ("axe", "bow"), ("glove", "glove"))

# Weapon Flow's third weapon (X; som_disc_third_weapon in som_disciplines.h).
THIRD_WEAPONS = {8: "bow"}
BUSHIDO = 10                                  # X = Iaijutsu, no spells
MARTIAL_ARTS = 1                              # X = flying kick
# Ninjutsu's B is Taijutsu: glove, glove, sword (som_ai_proxy.c). A ninja
# never turns monk for fists (player, 2026-11-19).
NINJUTSU = 2
TAIJUTSU_WEAPONS = ("glove", "sword")
# The proxy's B chains (som_b_chain): the weapons B may put in hand per class.
# Weapon Flow flows through weapons beyond its own three (player, 2026-11-20).
WEAPON_FLOW = 8
B_CHAIN_WEAPONS = {NINJUTSU: TAIJUTSU_WEAPONS, WEAPON_FLOW: ("sword", "axe", "whip", "javelin")}
# Right after the director switches, the game's saved class may still show
# the old one for a moment: no player pick is read from it meanwhile.
ADOPT_SETTLE_S = 3.0
SWORD_CLASSES = tuple(i for i, (b, _) in enumerate(WEAPONS) if b == "sword")
DODGE_CLASSES = (0, 1, 2, 3, 9, 10, 11, 12, 14)  # som_disc_has_dodge (3: Blink step)
ATTACK_MAGIC = (2, 3, 4, 5, 6, 7, 9, 11, 13, 15)  # classes with a hostile spell (som_magic.h)
# Classes whose X spells are self buffs worth casting as a fight starts:
# Swordsmanship's sabers, Martial Arts' body buffs, Support & Dance.
BUFF_CLASSES = (0, 1, 7)
NO_MAGIC = (8, 10, 12, 14)        # Weapon Flow, Bushido, Hunter, Artificer: X is not a spell
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
    # Late-added disciplines need situational reasons to be chosen, rather
    # than remaining names the director almost never selects.
    ("crowd", 12, 2, False, "Holy Water and Grand Cross punish a packed crowd"),
    ("tough", 13, 2, True, "Energy Absorb can sustain the Necromancer"),
    ("crowd", 13, 1, False, "a raised ally helps screen a crowd"),
    ("crowd", 14, 4, False, "bombs and proximity mines control packed enemies"),
    ("tough", 14, 1, False, "mines reward preparation against a hard target"),
    ("duel", 15, 3, False, "Mind Control turns an ordinary enemy"),
    ("crowd", 15, 2, True, "Sleep Flower and Balloon restrain a crowd"),
)
TOUGH_HP = 200                       # enemy max HP that counts as tough

LINES = {
    0: "Steel is my answer!", 1: "Fists of the falls!", 2: "Shadows, hide me!",
    3: "Mana, burn bright!", 4: "Light, mend us!", 5: "Darkness, feed on them!",
    6: "Spirits, answer my call!", 7: "Dance with me!", 8: "Every blade, one flow!",
    9: "They won't see me coming.", 10: "One cut, one life.", 11: "Steady... loose!",
    12: "The night is mine to hunt!", 13: "Rise and serve!", 14: "Built it myself!",
    15: "Your mind is mine.",
}

# Hysteresis: situations change every fight, so a new class must beat the
# current one clearly and the current one is kept at least MIN_HOLD_S
# (it flipped between five classes every 1-3 minutes on 2026-11-16).
SWITCH_MARGIN = 3
MIN_HOLD_S = 90.0
COMPANION_HOLD_S = 30.0              # girl and sprite change roles sooner
# A switch the game has not shown yet: the old class read meanwhile is lag,
# not a player pick. After this long the switch did not take.
CONFIRM_S = 15.0
PICK_KEEP_S = 12 * 3600.0            # a player's pick survives restarts this long
# Sister classes (the player's family, 2026-11-19): Martial Arts, Ninjutsu,
# Roguecraft, Bushido. A hero in one of them switches between them in a
# fight by situation, at most every SISTER_HOLD_S, even with a class the
# player picked; calm again, he goes back to the class he was in before.
SISTERS = (1, 2, 9, 10)
SISTER_HOLD_S = 8.0
SISTER_PICKS = (            # first match wins: (tag, class, why)
    ("hurt", 9, "hurt: Roguecraft cartwheels away and shoots from range"),
    ("far", 2, "enemy far: Ninjutsu's boomerang, Fuma clone and swap"),
    ("tough", 10, "tough enemy: Bushido's Iaijutsu"),
    ("crowd", 10, "crowd: Bushido's Iaijutsu cuts them all"),
    ("duel", 1, "one enemy close: Martial Arts combo and flying kick"),
)
CONTACT_PX = 40                      # no switching with an enemy this close
DEFAULT_COOLDOWN_S = 50.0


PLAN_SIZE = 3                        # classes in the wanted build
PLAN_BONUS = 2                       # fight score bonus for a wanted class
TRAIN_GAP = 1                        # level gap before rotating to train
PLAN_MIN_SAMPLES = 60                # decisions before a class's record counts
PLAN_CURIOSITY = 3                   # an untried class's stand-in record value
PLAN_RECORD_CAP = 6                  # learned record clamp (per-decision score)
HP_WEIGHT = 2.0                      # HP lost counts double (as som_tactics)
# Team scoring: a plan is judged as a team of PLAN_SIZE classes.
TEAM_COVER_BONUS = 2                 # per fight situation the team handles
TEAM_SHARED_SKILL = 1                # per extra member sharing Kawarimi (dodge)
TEAM_SHARED_WEAPON = 1               # per weapon two or more members train
TEAM_SITUATIONS = ("crowd", "tough", "duel", "far", "hurt")

# Named teams the player can ask for in one word (--wanted-classes shinobi).
ARCHETYPES = {
    "shinobi": (2, 10, 1),           # Ninjutsu, Bushido, Martial Arts
    "knight": (0, 8, 4),             # Swordsmanship, Weapon Flow, Divine Arts
    "ranger": (11, 9, 2),            # Archery, Roguecraft, Ninjutsu
    "mage": (3, 5, 6),               # Magic, Dark Arts, Summoning
    "bard": (7, 4, 3),               # Support & Dance, Divine Arts, Magic
}


def parse_classes(text):
    """Class indices from "Bushido, archery, 8" (names, prefixes or numbers)
    or an archetype name ("shinobi", see ARCHETYPES)."""
    picked = []
    for part in (text or "").split(","):
        part = part.strip().lower()
        if not part:
            continue
        if part in ARCHETYPES:
            picked += [i for i in ARCHETYPES[part] if i not in picked]
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


def _weapons(index):
    return set(WEAPONS[index]) | ({THIRD_WEAPONS[index]} if index in THIRD_WEAPONS else set())


def team_key(classes):
    return ",".join(str(i) for i in sorted(classes))


def team_score(team, values, magic):
    """(score, covered situations) of a team from its members' values plus
    synergy: situations covered, a shared Kawarimi, shared weapons."""
    covered = {tag for tag, klass, _, needs_mp, _ in ADVANTAGES
               if klass in team and tag in TEAM_SITUATIONS and (magic or not needs_mp)}
    dodgers = sum(1 for i in team if i in DODGE_CLASSES)
    counts = {}
    for i in team:
        for weapon in _weapons(i):
            counts[weapon] = counts.get(weapon, 0) + 1
    shared = sum(1 for n in counts.values() if n >= 2)
    value = (sum(values[i] for i in team) + TEAM_COVER_BONUS * len(covered) +
             TEAM_SHARED_SKILL * max(0, dodgers - 1) + TEAM_SHARED_WEAPON * shared)
    return value, covered


def plan_classes(build, record=None, max_mp=0, size=PLAN_SIZE, team_record=None):
    """(wanted classes, why): the AI's own build plan, chosen as a team.

    Each class is worth its stat fit (the build bonus, doubled) plus how it
    has actually fought (record {class: (samples, dealt, taken)}; untried
    classes get PLAN_CURIOSITY so the hero gives them a fair go). A team
    adds synergy (team_score) and its own record (team_record {team_key:
    (samples, dealt, taken)}), so teams that really work together win.
    Spell-only strengths need MP, so without MP the healers drop out.
    Ties go to the team fights have already played most.
    """
    record, team_record = record or {}, team_record or {}
    bonus = BUILD_BONUS.get(build, BUILD_BONUS["balanced"])
    magic = (max_mp or 0) >= MIN_SPELL_MP
    values, learned = {}, {}
    for index in range(len(NAMES)):
        if index in HEALERS and not magic:
            continue
        learned[index] = record_score(*record.get(index, (0, 0, 0)))
        values[index] = 2 * bonus.get(index, 0) + (
            PLAN_CURIOSITY if learned[index] is None else learned[index])
    best = None
    for team in itertools.combinations(sorted(values), min(size, len(values))):
        value, covered = team_score(team, values, magic)
        together = record_score(*team_record.get(team_key(team), (0, 0, 0)))
        if together is not None:
            value += len(team) * together     # the record speaks for every member
        played = sum(record.get(i, (0, 0, 0))[0] for i in team)
        rank = (value, played, -sum(team))
        if best is None or rank > best[0]:
            best = (rank, team, covered, together)
    if best is None:
        return [], f"{build} build; no class fits"
    _, team, covered, together = best
    parts = [f"{NAMES[i]} {'untried' if learned[i] is None else f'{learned[i]:+.1f}'}"
             for i in team]
    name = next((n for n, members in ARCHETYPES.items() if set(members) == set(team)), None)
    why = (f"{build} build; " + (f"{name}: " if name else "") + ", ".join(parts) +
           f"; covers {', '.join(t for t in TEAM_SITUATIONS if t in covered) or 'nothing'}")
    if together is not None:
        why += f"; together {together:+.1f}"
    return list(team), why


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
    def __init__(self, clock=None, min_hold=MIN_HOLD_S):
        import time
        self.min_hold = min_hold
        self.unconfirmed = None      # class before a switch not yet seen in the game
        self.clock = clock or time.monotonic
        self.current = None          # what this director last asked for
        self.switched_at = None
        self.cooldown = DEFAULT_COOLDOWN_S   # None = never switch (gambit 0)
        self.wanted = ()             # the wanted build (class indices)
        self.wanted_fixed = False    # set by the player: the AI keeps it
        self.pinned = False          # the player picked the class in the game
        self.picked_at = None        # wall clock of that pick
        self.home = None             # sister class to return to when calm

    def sister_choose(self, hp_fraction=1.0, enemies=0, nearest=None, toughest=0):
        """(sister class, reason) for this moment of a fight, the home class
        once calm, or None. Only for a hero already in a sister class."""
        if self.current not in SISTERS or self.cooldown is None:
            return None
        tags = situation_tags(hp_fraction, enemies, nearest, toughest)
        if "calm" in tags:
            home, self.home = self.home, None
            if home is not None and home != self.current:
                return home, f"fight over: back to {NAMES[home]}"
            return None
        if self.switched_at is not None and self.clock() - self.switched_at < SISTER_HOLD_S:
            return None
        origin = self.home if self.home is not None else self.current
        for tag, klass, why in SISTER_PICKS:
            if tag in tags:
                if origin == NINJUTSU and klass == MARTIAL_ARTS:
                    continue         # Taijutsu is the ninja's fists: stay a ninja
                if klass == self.current:
                    return None
                if self.home is None:
                    self.home = self.current
                return klass, why
        return None

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
        sister = self.sister_choose(**situation)
        if sister is not None or (self.current in SISTERS and self.pinned):
            return sister
        if self.pinned:
            return None
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
                        self.clock() - self.switched_at < max(self.cooldown, self.min_hold)):
                    return None
                if need_cut:         # already holds a sword: keep it
                    return None
                if (best_value is not None and best_value <
                        self._score(self.current, build, levels, max_mp, tags)[0] + SWITCH_MARGIN):
                    return None
        reason = ("grass ahead needs a sword" if need_cut else
                  why or f"fits a {build} hero best (score {best_value})")
        return best, reason

    def adopt(self, selected):
        """The class selected in the game (None = none). When it is not what
        this director asked for, the player picked it: the director takes it
        as current and stops switching. Returns True when that happened."""
        if selected is not None and selected == self.current:
            self.unconfirmed = None
            return False
        if selected is None:
            return False
        if (self.switched_at is not None and
                self.clock() - self.switched_at < ADOPT_SETTLE_S):
            return False
        if selected == self.unconfirmed:
            if self.clock() - self.switched_at < CONFIRM_S:
                return False
            self.current, self.unconfirmed = selected, None   # it did not take
            self.home = None
            self.switched_at = self.clock()
            return False
        self.unconfirmed = self.home = None
        self.current, self.pinned, self.picked_at = selected, True, None
        self.switched_at = self.clock()
        return True

    def initialize(self, selected, saved=None, now=None):
        """Seed the observed class without treating startup state as a pick.

        saved: an earlier pick_state(). A recent pick is kept only while the
        game still shows that class."""
        import time
        now = time.time() if now is None else now
        self.current = selected
        self.unconfirmed = None
        self.pinned = bool(
            selected is not None and isinstance(saved, dict) and
            saved.get("class") == selected and
            isinstance(saved.get("at"), (int, float)) and
            0 <= now - saved["at"] < PICK_KEEP_S)
        self.picked_at = saved["at"] if self.pinned else None
        home = saved.get("home") if self.pinned else None
        self.home = home if home in SISTERS else None
        self.switched_at = self.clock() if selected is not None else None

    def pick_state(self, now=None):
        """The player's pick to store across restarts, or None."""
        import time
        if not self.pinned:
            return None
        if self.picked_at is None:
            self.picked_at = time.time() if now is None else now
        return {"class": self.current, "home": self.home, "at": self.picked_at}

    def switched(self, index):
        self.unconfirmed = self.current if index != self.current else None
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
            connection.execute(
                "CREATE TABLE IF NOT EXISTS team_stats (team TEXT PRIMARY KEY, "
                "samples INTEGER NOT NULL, dealt INTEGER NOT NULL, taken INTEGER NOT NULL)")

    def stats(self):
        return {int(k): (n, d, t) for k, n, d, t in self.db.execute(
            "SELECT class,samples,dealt,taken FROM class_stats")}

    def team_stats(self):
        return {team: (n, d, t) for team, n, d, t in self.db.execute(
            "SELECT team,samples,dealt,taken FROM team_stats")}

    def record(self, observation, klass, team=None):
        """team: the wanted build in force; its member's fights count for it."""
        from som_tactics import _nearest
        hero = ((observation or {}).get("party") or [{}])[0] or {}
        hp, max_hp = hero.get("hp"), hero.get("max_hp") or 0
        map_id = ((observation or {}).get("tile_context") or {}).get("map_id")
        if self.pending is not None and isinstance(hp, int):
            before_class, slot, identity, hero_before, enemy_before, pending_map, \
                before_team = self.pending
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
                    if before_team:
                        self.db.execute(
                            "INSERT INTO team_stats VALUES(?,1,?,?) ON CONFLICT(team) DO UPDATE "
                            "SET samples=samples+1,dealt=dealt+excluded.dealt,"
                            "taken=taken+excluded.taken", (before_team, dealt, lost))
        self.pending = None
        enemy = _nearest(observation)
        if (klass is not None and enemy is not None and isinstance(hp, int) and hp > 0 and
                isinstance(enemy.get("hp"), int)):
            # Only fights under a member of the team count for the team.
            together = team_key(team) if team and klass in team else None
            self.pending = (klass, enemy.get("slot"), enemy.get("identity"), hp,
                            enemy["hp"], map_id, together)

"""Quest: where the next trip goes to advance the story or to level up.

The gambit page QUEST (proxy src/som_gambit.h) has two weights, "Advance
the story" and "Level up". This module turns them into a hunting target
for the play loop, from the game's own data (cidwel/som-decomp
docs/maps.md 2 and 10, docs/cutscene-engine.md 5, docs/rom-combat.md 2.1):

- Boss spawns: every map's 8-byte object records ($C8:7000 + 2*m, after 8
  header bytes); object ids 0x57-0x7F are bosses and exist while
  min <= (event flag $7E:CF00[f] & 15) <= max. A boss whose flag has moved
  past its range is beaten.
- Levels: monster record byte 0 at file offset 0x101C00 + id*29.

Story target = the lowest-level boss that is not beaten (flag past its
range, a kill this module saw, or BEHIND_LEVELS below the hero), lies on a
reachable map and is not a map the trips failed to reach. Bosses kept
spawned for the whole game (range 0-15) count only through the kill record
or the level rule. Level gate: below the boss's level plus a margin set by
the "Level up" weight, the trip goes to the reachable map whose monsters
sit closest above the hero's level instead. Nothing reachable: None, and
the play loop explores new maps (som_frontier), "explore beyond the table".

The ROM-derived tables are cached like the exit tables (som_rom), keyed by
the ROM's SHA-1; no ROM bytes are stored in the repository.
"""
import json
import struct
from pathlib import Path

MAP_POINTERS = 0x087000          # $C8:7000, 2 bytes per map (bank C8 offsets)
MAP_COUNT = 511
HEADER = 8
RECORD = 8
MONSTER_RECORDS = 0x101C00       # 29 bytes per monster id
MONSTER_SIZE = 29
BOSS_FIRST, BOSS_LAST = 0x57, 0x7F
FLAG_BASE = 0x7ECF00
BEHIND_LEVELS = 6                # a boss this far below the hero is behind him
GRIND_BELOW, GRIND_ABOVE = 1, 4  # monster levels around the hero worth grinding
DYING = 0xF9                     # a monster slot's identity while it dies
# Self-adjusting level-up (2026-11-20): Turbo's monsters hit far above their
# ROM levels (level 1-5 Mushbooms and Lullabuds on map 16 took 20+ HP from a
# level 11 hero), so the boss-level gate cannot see that the way is too hard.
# STORY_FAILURES story trips in a row that end in a retreat or a death before
# arriving raise a level floor by one; until the hero reaches it, trips go
# to level up on the map where the last story trip failed.
STORY_FAILURES = 2


def _bank_c8(offset):
    return 0x080000 + offset


def build(rom):
    """{"bosses": [...], "monsters": {map: [levels]}} from a 4 MiB Turbo ROM."""
    def level(oid):
        return rom[MONSTER_RECORDS + oid * MONSTER_SIZE]
    pointers = [struct.unpack_from("<H", rom, MAP_POINTERS + 2 * m)[0]
                for m in range(MAP_COUNT + 1)]
    bosses, monsters = [], {}
    for m in range(MAP_COUNT):
        start, end = pointers[m], pointers[m + 1]
        if end <= start + HEADER or end - start > 4096:
            continue
        for o in range(_bank_c8(start) + HEADER, _bank_c8(end) - RECORD + 1, RECORD):
            flag, bounds, x, y, _, oid = rom[o:o + 6]
            if BOSS_FIRST <= oid <= BOSS_LAST:
                bosses.append({"map": m, "id": oid, "flag": flag, "min": bounds >> 4,
                               "max": bounds & 15, "x": x & 0x7F, "y": y,
                               "level": level(oid)})
            elif oid < BOSS_FIRST:
                monsters.setdefault(str(m), []).append(level(oid))
    return {"bosses": bosses, "monsters": monsters}


def load(fetch_rom, cache_path, fetch_digest=None):
    """Tables from the cache when the ROM digest matches, else from fetch_rom()."""
    cache_path = Path(cache_path)
    try:
        cached = json.loads(cache_path.read_text())
    except (OSError, ValueError):
        cached = None
    digest = fetch_digest() if fetch_digest is not None else None
    if cached and digest and cached.get("digest") == digest:
        return cached
    rom = fetch_rom()
    if len(rom) < MONSTER_RECORDS + 0x80 * MONSTER_SIZE:
        raise ValueError("ROM too short for the quest tables")
    data = build(rom)
    data["digest"] = digest
    try:
        cache_path.parent.mkdir(parents=True, exist_ok=True)
        temporary = cache_path.with_suffix(".tmp")
        temporary.write_text(json.dumps(data))
        temporary.replace(cache_path)
    except OSError:
        pass
    return data


class Quest:
    def __init__(self, data, history=None, log=print):
        self.bosses = sorted(data.get("bosses") or [], key=lambda b: (b["level"], b["map"]))
        self.monsters = {int(m): levels for m, levels in (data.get("monsters") or {}).items()}
        self.history, self.log = history, log
        self.flags = None                 # 256 bytes of $7E:CF00, or None
        self.weights = (3, 3)             # story, level up (gambit page QUEST)
        self.boss_slots = {}              # monster slot -> (boss id, map)
        self.last = None
        self.here = None                  # map of the latest choice
        self.hero_level = None            # 1-based, from the latest choice

    # Inputs each decision ------------------------------------------------
    def update(self, wram=None, weights=None, observation=None):
        if wram is not None:
            try:
                self.flags = bytes(wram.u8(FLAG_BASE + i) for i in range(256))
            except (IndexError, ValueError, TypeError):
                self.flags = None
        if weights is not None:
            self.weights = tuple(weights)
        if observation is not None:
            self._watch_bosses(observation)

    def _watch_bosses(self, observation):
        """A boss whose slot turns to the dying identity is beaten."""
        here = ((observation.get("tile_context") or {}).get("map_id")
                if isinstance(observation, dict) else None)
        for enemy in (observation or {}).get("enemy_slots") or []:
            if not isinstance(enemy, dict):
                continue
            slot, identity = enemy.get("slot"), enemy.get("identity")
            if isinstance(identity, int) and BOSS_FIRST <= identity <= BOSS_LAST and \
                    enemy.get("active"):
                self.boss_slots[slot] = (identity, here)
            elif slot in self.boss_slots and identity == DYING:
                boss, boss_map = self.boss_slots.pop(slot)
                self._beaten(boss, boss_map)

    def _beaten(self, boss, boss_map):
        beaten = self.defeated()
        key = f"{boss}@{boss_map}"
        if key not in beaten and self.history is not None:
            beaten.add(key)
            self.history.set_agent_state("bosses_defeated", sorted(beaten))
            self.log(f"[quest] boss 0x{boss:02x} on map {boss_map} defeated")

    def defeated(self):
        value = self.history.get_agent_state("bosses_defeated") if self.history else None
        return set(value) if isinstance(value, list) else set()

    # Self-adjustment --------------------------------------------------------
    def adjustment(self):
        value = self.history.get_agent_state("quest_adjust") if self.history else None
        return value if isinstance(value, dict) else {"failures": 0}

    def _save_adjustment(self, value):
        if self.history is not None:
            self.history.set_agent_state("quest_adjust", value)

    def story_outcome(self, outcome, raw_level=None, map_id=None):
        """A story trip ended: "arrived", "retreated" (low HP) or "died"."""
        adjust = self.adjustment()
        hero = (raw_level + 1) if isinstance(raw_level, int) else self.hero_level
        if outcome == "arrived":
            if adjust.get("failures"):
                self.log("[quest] story trip arrived; failure count reset")
            adjust["failures"] = 0
            self._save_adjustment(adjust)
            return
        adjust["failures"] = adjust.get("failures", 0) + 1
        # Only a map with monsters is somewhere to level up: a failure
        # noticed in the inn house (map 259) made him "hunt" there for 30
        # minutes (2026-11-20).
        if map_id is not None and self.monsters.get(map_id):
            adjust["failed_on"] = map_id
        self.log(f"[quest] story trip {outcome} on map {map_id} before arriving "
                 f"({adjust['failures']}/{STORY_FAILURES})")
        if adjust["failures"] >= STORY_FAILURES and isinstance(hero, int):
            floor = max(adjust.get("floor") or 0, hero + 1)
            adjust.update(floor=floor, failures=0, grind_map=adjust.get("failed_on"))
            if adjust["grind_map"] is None:
                self.log("[quest] adjusting skipped: no failure on a map with monsters yet")
                adjust.pop("floor", None)
                self._save_adjustment(adjust)
                return
            self.log(f"[quest] adjusting: {STORY_FAILURES} story trips in a row ended "
                     f"early; level up to {floor} on map {adjust['grind_map']} "
                     f"before the story again (hero level {hero})")
        self._save_adjustment(adjust)

    def _level_floor(self, hero, reachable):
        """(map, reason) to level up on while below the learned floor, or None."""
        adjust = self.adjustment()
        floor = adjust.get("floor")
        if not floor:
            return None
        if hero >= floor:
            self.log(f"[quest] reached level {hero} (floor {floor}); back to the story")
            adjust.pop("floor", None)
            adjust.pop("grind_map", None)
            self._save_adjustment(adjust)
            return None
        grind = adjust.get("grind_map")
        if not self.monsters.get(grind):
            grind = adjust.get("failed_on")
        if not self.monsters.get(grind) or not (grind == self.here or reachable(grind)):
            return None
        return grind, (f"level up first: story trips kept failing on the way; hero "
                       f"{hero} < learned floor {floor}, hunting map {grind}")

    # Choice -----------------------------------------------------------------
    def _blocked_maps(self):
        state = self.history.get_agent_state("frontier") if self.history else None
        failed = (state or {}).get("failed", {}) if isinstance(state, dict) else {}
        return {int(m) for m, tries in failed.items() if tries >= 2}

    def story_boss(self, hero_level, reachable):
        """The next boss the story wants, or None."""
        beaten, blocked = self.defeated(), self._blocked_maps()
        for boss in self.bosses:
            if f"{boss['id']}@{boss['map']}" in beaten or boss["map"] in blocked:
                continue
            if boss["level"] + BEHIND_LEVELS <= hero_level:
                continue
            if self.flags is not None:
                value = self.flags[boss["flag"]] & 15
                if not boss["min"] <= value <= boss["max"]:
                    continue          # flag past (or before) its range: not now
            if reachable(boss["map"]):
                return boss
            approach = self._approach(boss["map"], reachable)
            if approach is not None:
                return dict(boss, approach=approach)
        return None

    def _approach(self, boss_map, reachable):
        """A reachable map whose event scripts lead into boss_map (the boss
        rooms are entered by scripts, often behind a story event that has
        not happened yet), nearest first; None without one."""
        tables = getattr(self.history, "rom_tables", None)
        if tables is None or not hasattr(tables, "scripted_into"):
            return None
        blocked = self._blocked_maps()
        here = getattr(self, "here", None)

        grid = getattr(self.history, "world_grid", None)
        walked = (grid.distances(here, tables.neighbours)
                  if grid is not None and here is not None else {})

        def hops(map_id):
            if map_id in walked:              # walked tiles on the world grid
                return (walked[map_id], map_id)
            path = (self.history.map_path(here, map_id)
                    if here is not None and hasattr(self.history, "map_path") else None)
            return ((len(path) * 64) if path else 10 ** 6, map_id)
        doors = sorted((m for m in tables.scripted_into(boss_map)
                        if m not in blocked and reachable(m)), key=hops)
        return doors[0] if doors else None

    def grind_map(self, hero_level, reachable, here=None):
        """Reachable map whose monsters are closest above the hero's level."""
        blocked, best = self._blocked_maps(), None
        for map_id, levels in self.monsters.items():
            useful = [lv for lv in levels
                      if hero_level - GRIND_BELOW <= lv <= hero_level + GRIND_ABOVE]
            if not useful or map_id in blocked:
                continue
            mean = sum(useful) / len(useful)
            # Each monster is worth most one level above the hero and less
            # the farther off: a map full of good ones beats one stray.
            value = sum(1 - abs(lv - (hero_level + 1)) / (GRIND_ABOVE + 1) for lv in useful)
            score = (-value, map_id != here)
            if (best is None or score < best[0]) and (map_id == here or reachable(map_id)):
                best = (score, map_id, mean, len(useful))
        return best[1:] if best else None

    def choose(self, summary, reachable):
        """(map_id, reason) for the next trip, or None (explore instead)."""
        story_w, level_w = self.weights
        if not story_w and not level_w:
            return None
        raw = summary.get("level")
        hero = (raw + 1) if isinstance(raw, int) else 1   # +0x181 is 0-based
        here = summary.get("map_id")
        self.here, self.hero_level = here, hero
        floor = self._level_floor(hero, reachable) if story_w else None
        if floor is not None:
            return self._pick(*floor)
        boss = self.story_boss(hero, reachable) if story_w else None
        if boss is not None:
            gate = boss["level"] + max(0, level_w - 2)
            if level_w and hero < gate:
                grind = self.grind_map(hero, reachable, here)
                if grind is not None:
                    map_id, mean, count = grind
                    return self._pick(map_id, f"level up first: hero {hero} < {gate} for boss "
                                      f"0x{boss['id']:02x} (level {boss['level']}) on map "
                                      f"{boss['map']}; {count} monsters near level {mean:.0f}")
            if boss.get("approach") is not None:
                return self._pick(boss["approach"], f"story: boss 0x{boss['id']:02x} (level "
                                  f"{boss['level']}) on map {boss['map']} is entered by a "
                                  f"story event from map {boss['approach']}; go there, "
                                  f"hero level {hero}")
            return self._pick(boss["map"], f"story: boss 0x{boss['id']:02x} (level "
                              f"{boss['level']}) on map {boss['map']}, hero level {hero}")
        if level_w and not story_w:
            grind = self.grind_map(hero, reachable, here)
            if grind is not None:
                map_id, mean, count = grind
                return self._pick(map_id, f"level up: {count} monsters near level {mean:.0f}")
        return None

    def _pick(self, map_id, why):
        grid = getattr(self.history, "world_grid", None)
        where = grid.describe(self.here, map_id) if grid is not None and self.here else ""
        if where:
            why = f"{why} ({where} on the world grid)"
        if (map_id, why) != self.last:
            self.log(f"[quest] hunt map {map_id}: {why}")
            self.last = (map_id, why)
        return map_id, why

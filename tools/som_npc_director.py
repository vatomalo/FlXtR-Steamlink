#!/usr/bin/env python3
"""Daily NPC director for the living world (docs/SOM-LIVING-WORLD.md, 6.1).

Ordinary behaviour is the deterministic schedule. Once per game day and NPC
the director may change *one* future schedule slot, chosen from a small menu
that is built and validated here:

- every option is a `LivingWorld.override_day` the simulation already
  accepts (an existing entry time, a destination the NPC already uses);
- sleep and wake-up slots are never offered (night stays night);
- option "K" (keep the schedule) is always present and is the fallback for
  any error, timeout or reply that is not exactly one offered id.

Qwen only ever returns an option id. It never sees or produces addresses,
coordinates or commands, never runs per frame, and its latency cannot delay
the simulation: `DailyDirector.tick` never blocks; the call runs in a worker
thread and its answer is applied on a later tick if it is still in the future.
"""
import json
import re
import threading
import urllib.request

KEEP = "K"
MAX_OPTIONS = 6
DIRECTOR_TIMEOUT = 45
FIXED_ACTIVITIES = {"sleep", "wake_up"}
OPTION_IDS = "ABCDEF"
QWEN_OPTIONS = {"temperature": 0.4, "num_predict": 8}


def ensure_tables(db):
    with db:
        db.execute("CREATE TABLE IF NOT EXISTS npc_director_days (npc_id TEXT NOT NULL, "
                   "day INTEGER NOT NULL, choice TEXT NOT NULL, source TEXT NOT NULL, "
                   "PRIMARY KEY (npc_id, day))")


def day_options(world, npc_id, day, after_minute=-1):
    """[(id, minute, destination, label)] of valid one-slot changes for `day`.

    Only entries starting after `after_minute` (minute of day) are offered so
    a choice never rewrites the past. Places are interleaved in order of
    personality appeal (diligent: workplace first; sociable: social places;
    unsociable: home), so the capped menu leads with in-character choices.
    """
    npc = world.world.npcs[npc_id]
    places = sorted({npc.resolve_name(e.destination) for e in npc.entries} | {npc.home}
                    - {None})
    traits = npc.personality
    social = {npc.resolve_name(e.destination) for e in npc.entries
              if e.activity in ("socialize", "eat")} - {npc.home}

    def appeal(place):
        if place == npc.workplace:
            return traits.get("diligent", 0.5)
        if place == npc.home:
            return 1.0 - traits.get("sociable", 0.5)
        if place in social:
            return traits.get("sociable", 0.5)
        return 0.3

    by_place = {}
    for entry in npc.entries:
        if entry.activity in FIXED_ACTIVITIES or entry.minute <= after_minute:
            continue
        current = npc.resolve_name(entry.destination)
        for place in places:
            if place != current:
                by_place.setdefault(place, []).append((entry.minute, entry.activity))
    for slots in by_place.values():
        # Evening slots first, travel slots last: "work late", "stay out",
        # "stay in tonight" are the changes that read as character.
        slots.sort(key=lambda slot: (slot[1] == "travel", -slot[0]))
    # Round-robin over places, most appealing first, so a capped menu offers
    # every kind of change (work, social, home) instead of one place only.
    order = sorted(by_place, key=lambda place: (-appeal(place), place))
    picked = []
    while len(picked) < MAX_OPTIONS and any(by_place.values()):
        for place in order:
            if by_place[place] and len(picked) < MAX_OPTIONS:
                minute, activity = by_place[place].pop(0)
                picked.append((minute, place, activity))
    options = []
    for oid, (minute, place, activity) in zip(OPTION_IDS, picked):
        hh, mm = divmod(minute, 60)
        options.append((oid, minute, place, f"at {hh:02d}:{mm:02d} {activity} at {place} instead"))
    return options


def build_prompt(world, npc_id, day, options, talks_yesterday=0):
    npc = world.world.npcs[npc_id]
    traits = npc.personality
    lines = [
        f"You decide one change to the daily routine of {npc.name}, a villager.",
        f"Personality: {', '.join(f'{k} {v:.1f}' for k, v in sorted(traits.items())) or 'plain'}.",
        f"Day {day}. The player talked to this villager {talks_yesterday} times yesterday.",
        "Options:",
        f"{KEEP}: keep the usual routine",
    ]
    lines += [f"{oid}: {label}" for oid, _, _, label in options]
    lines.append("Answer with exactly one option letter.")
    return "\n".join(lines)


def parse_choice(raw, options):
    """The single offered id in `raw`, else KEEP."""
    allowed = {KEEP} | {oid for oid, *_ in options}
    found = set(re.findall(r"(?<![A-Za-z])([A-Z])(?![A-Za-z])", raw or ""))
    found &= allowed
    return found.pop() if len(found) == 1 else KEEP


def qwen_chooser(url, model, timeout=DIRECTOR_TIMEOUT):
    """chooser(prompt) -> raw text via Ollama /api/generate."""
    def choose(prompt):
        payload = json.dumps({"model": model, "prompt": prompt, "stream": False,
                              "think": False, "keep_alive": "30m",
                              "options": QWEN_OPTIONS}).encode()
        req = urllib.request.Request(url.rstrip("/") + "/api/generate", data=payload,
                                     headers={"Content-Type": "application/json"})
        with urllib.request.urlopen(req, timeout=timeout) as response:
            return json.load(response).get("response") or ""
    return choose


class DailyDirector:
    """At most one director decision per NPC and game day, never blocking."""

    def __init__(self, world, chooser, background=True):
        self.world, self.chooser, self.background = world, chooser, background
        self.pending = {}            # (npc_id, day) -> thread
        self.answers = {}            # (npc_id, day) -> (options, raw or None)
        self.lock = threading.Lock()
        ensure_tables(world.db)

    def decided(self, npc_id, day):
        row = self.world.db.execute("SELECT choice FROM npc_director_days WHERE npc_id=? "
                                    "AND day=?", (npc_id, day)).fetchone()
        return row[0] if row else None

    def _record(self, npc_id, day, choice, source):
        with self.world.db:
            self.world.db.execute("INSERT OR REPLACE INTO npc_director_days VALUES (?,?,?,?)",
                                  (npc_id, day, choice, source))

    def _ask(self, key, options, prompt):
        try:
            raw = self.chooser(prompt)
        except Exception:            # timeout, connection, bad JSON: keep schedule
            raw = None
        with self.lock:
            self.answers[key] = (options, raw)

    def tick(self, now):
        """Start or apply today's decisions. Returns [(npc_id, choice)] applied."""
        applied = []
        day = now.day
        for npc_id in self.world.world.npcs:
            key = (npc_id, day)
            if self.decided(npc_id, day) is not None:
                continue
            with self.lock:
                answer = self.answers.pop(key, None)
            if answer is not None:
                self.pending.pop(key, None)
                applied.append((npc_id, self._apply(npc_id, day, now, *answer)))
                continue
            if key in self.pending:
                continue
            options = day_options(self.world, npc_id, day, now.minute_of_day)
            if not options:
                self._record(npc_id, day, KEEP, "no_options")
                applied.append((npc_id, KEEP))
                continue
            talks = 0
            if day:
                talks = (self.world.encounters(npc_id, (day - 1) * 1440)
                         - self.world.encounters(npc_id, day * 1440))
            prompt = build_prompt(self.world, npc_id, day, options, talks)
            if self.background:
                thread = threading.Thread(target=self._ask, args=(key, options, prompt),
                                          daemon=True)
                self.pending[key] = thread
                thread.start()
            else:
                self._ask(key, options, prompt)
                with self.lock:
                    answer = self.answers.pop(key)
                applied.append((npc_id, self._apply(npc_id, day, now, *answer)))
        return applied

    def _apply(self, npc_id, day, now, options, raw):
        if raw is None:
            self._record(npc_id, day, KEEP, "error")
            return KEEP
        choice = parse_choice(raw, options)
        source = "qwen"
        if choice != KEEP:
            _, minute, place, _ = next(o for o in options if o[0] == choice)
            if minute <= now.minute_of_day:
                choice, source = KEEP, "stale"     # answered after the slot began
            else:
                try:
                    self.world.override_day(npc_id, day, minute, place, source="director")
                except ValueError:
                    choice, source = KEEP, "rejected"
        self._record(npc_id, day, choice, source)
        return choice

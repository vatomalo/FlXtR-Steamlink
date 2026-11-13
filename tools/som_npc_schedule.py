#!/usr/bin/env python3
"""Data-driven NPC schedules (Ultima VII style) for the living world.

A schedule file is JSON (no extra dependency):

    {"version": 1,
     "locations": {"<id>": {"map": 131, "x": 27, "y": 25, "layer": 1,
                             "evidence": "...", "abstract": false}},
     "npcs": [{"id": "potos_villager_c7", "home": "<location>",
               "workplace": "<location>",
               "native": {"map": 131, "record": 8, "kind": 199, "static": true},
               "story": [{"flag": 16, "min": 0, "max": 6}],
               "schedule": [{"time": "06:00", "activity": "wake_up",
                             "destination": "home"}, ...],
               "lines": {"sleep": "Zzz...", ...}}]}

`destination` is a location id or one of the aliases `home`, `workplace`,
`stay` (keep the current place). Locations marked `abstract` (house
interiors on another map, for example) exist in the off-screen simulation;
their `proxy` names the on-map place the native NPC stands at instead.
Every concrete location must say how it was verified (`evidence`).
"""
import json
from pathlib import Path

from som_world_clock import MINUTES_PER_DAY, format_time, parse_time

ACTIVITIES = ("wake_up", "eat", "travel", "work", "socialize", "wander", "patrol",
              "return_home", "sleep", "idle", "shop_open", "shop_closed")
ALIASES = ("home", "workplace", "stay")
DEFAULT_PATH = Path(__file__).with_name("som_schedules") / "potos.json"


class ScheduleError(ValueError):
    pass


class Location:
    def __init__(self, name, spec):
        self.name = name
        try:
            self.map, self.x, self.y = int(spec["map"]), int(spec["x"]), int(spec["y"])
        except (KeyError, TypeError, ValueError):
            raise ScheduleError(f"location {name}: map/x/y required") from None
        self.layer = int(spec.get("layer", 1))
        self.abstract = bool(spec.get("abstract", False))
        self.proxy = spec.get("proxy")
        self.facing = spec.get("facing")
        self.evidence = spec.get("evidence", "")
        if not self.evidence:
            raise ScheduleError(f"location {name}: say how it was verified (evidence)")
        if self.abstract and not self.proxy:
            raise ScheduleError(f"location {name}: abstract locations need a proxy")

    @property
    def tile(self):
        return self.x, self.y

    def __repr__(self):
        return f"Location({self.name} map {self.map} {self.x},{self.y})"


class Entry:
    def __init__(self, spec):
        self.minute = parse_time(spec["time"])
        self.activity = spec["activity"]
        if self.activity not in ACTIVITIES:
            raise ScheduleError(f"unknown activity {self.activity!r}")
        self.destination = spec.get("destination", "stay")

    def __repr__(self):
        return f"{format_time(self.minute)} {self.activity} @ {self.destination}"


class NpcSpec:
    def __init__(self, spec, locations):
        self.id = spec["id"]
        self.name = spec.get("name", self.id)
        self.home = spec.get("home")
        self.workplace = spec.get("workplace")
        self.native = dict(spec.get("native") or {})
        self.story = list(spec.get("story") or [])
        self.lines = dict(spec.get("lines") or {})
        self.personality = dict(spec.get("personality") or {})
        self.entries = sorted((Entry(item) for item in spec.get("schedule", ())),
                              key=lambda entry: entry.minute)
        if not self.entries:
            raise ScheduleError(f"{self.id}: empty schedule")
        if len({entry.minute for entry in self.entries}) != len(self.entries):
            raise ScheduleError(f"{self.id}: two entries at the same time")
        for entry in self.entries:
            target = self.resolve_name(entry.destination)
            if target is not None and target not in locations:
                raise ScheduleError(f"{self.id}: unknown destination {entry.destination!r}")
        for alias in (self.home, self.workplace):
            if alias is not None and alias not in locations:
                raise ScheduleError(f"{self.id}: unknown location {alias!r}")
        if self.native and not {"map", "record", "kind"} <= self.native.keys():
            raise ScheduleError(f"{self.id}: native binding needs map, record, kind")

    def resolve_name(self, destination):
        """Location id for a destination (None for `stay`)."""
        if destination == "home":
            return self.home
        if destination == "workplace":
            return self.workplace
        if destination == "stay":
            return None
        return destination

    def entry_at(self, minute_of_day):
        """The entry in force at minute_of_day (the last one wraps from yesterday)."""
        current = self.entries[-1]
        for entry in self.entries:
            if entry.minute <= minute_of_day % MINUTES_PER_DAY:
                current = entry
        return current

    def boundaries(self, start_minutes, end_minutes):
        """Absolute minutes in (start, end] at which an entry begins."""
        found = []
        first_day = int(start_minutes) // MINUTES_PER_DAY
        last_day = int(end_minutes) // MINUTES_PER_DAY
        for day in range(first_day, last_day + 1):
            for entry in self.entries:
                moment = day * MINUTES_PER_DAY + entry.minute
                if start_minutes < moment <= end_minutes:
                    found.append((moment, entry))
        return found

    def story_allows(self, flags):
        """False when a story flag range excludes this NPC (it is absent)."""
        for rule in self.story:
            value = flags.get(int(rule["flag"]))
            if value is None:
                continue          # unknown story state: do not override
            if not int(rule["min"]) <= value <= int(rule["max"]):
                return False
        return True


class World:
    """Locations plus NPC specs from one schedule file."""

    def __init__(self, data):
        if data.get("version") != 1:
            raise ScheduleError("schedule file version must be 1")
        self.locations = {name: Location(name, spec)
                          for name, spec in (data.get("locations") or {}).items()}
        for location in self.locations.values():
            if location.proxy and location.proxy not in self.locations:
                raise ScheduleError(f"{location.name}: unknown proxy {location.proxy!r}")
        self.npcs = {}
        for spec in data.get("npcs") or ():
            npc = NpcSpec(spec, self.locations)
            if npc.id in self.npcs:
                raise ScheduleError(f"duplicate NPC id {npc.id}")
            self.npcs[npc.id] = npc

    @classmethod
    def load(cls, path=DEFAULT_PATH):
        return cls(json.loads(Path(path).read_text()))

    def location(self, name):
        return self.locations[name] if name is not None else None

    def on_map(self, location, map_id):
        """The concrete place on map_id that stands for location, or None."""
        while location is not None and (location.abstract or location.map != map_id):
            if not location.abstract:
                return None
            location = self.locations.get(location.proxy)
        return location

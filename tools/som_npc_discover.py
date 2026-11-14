#!/usr/bin/env python3
"""Give more Potos villagers a daily life, from a live snapshot of the town.

The living world only moved one villager (record 8). This reads the town
while the hero stands in Potos (map 131, field mode), finds every native
NPC whose own AI is off (static: those are the ones the proxy can walk),
and plans for each a home (its spawn tile), a workplace and a meeting spot
on tiles it can actually reach on the live walkable grid. Each gets one of
a few daily routines, offset in time so the town does not move in step,
and its own lines. Read-only against the game; with --write the result is
merged into tools/som_schedules/potos.json (a .bak copy is kept) and the
whole file is validated by the schedule loader first.

    python3 tools/som_npc_discover.py            # plan only
    python3 tools/som_npc_discover.py --write    # add the villagers
"""
import argparse
import json
import shutil
import sys
from pathlib import Path

from som_npc_native import decode_npcs, plan_route, walkable_grid
from som_npc_schedule import DEFAULT_PATH, World

POTOS = 131
SEARCH = 8            # tiles around the spawn considered for spots
MIN_ROUTE, MAX_ROUTE = 3, 8

ROUTINES = {
    "worker": [("06:00", "wake_up", "home"), ("07:00", "eat", "home"),
               ("08:00", "travel", "workplace"), ("09:00", "work", "workplace"),
               ("12:00", "eat", "meeting"), ("13:00", "work", "workplace"),
               ("18:00", "socialize", "meeting"), ("21:00", "return_home", "home"),
               ("22:00", "sleep", "home")],
    "wanderer": [("07:00", "wake_up", "home"), ("08:00", "wander", "meeting"),
                 ("10:00", "wander", "workplace"), ("12:00", "eat", "home"),
                 ("14:00", "wander", "meeting"), ("17:00", "socialize", "workplace"),
                 ("20:00", "return_home", "home"), ("21:00", "sleep", "home")],
    "early_bird": [("05:00", "wake_up", "home"), ("05:30", "patrol", "workplace"),
                   ("08:00", "eat", "home"), ("09:00", "work", "workplace"),
                   ("15:00", "idle", "meeting"), ("19:00", "return_home", "home"),
                   ("20:00", "sleep", "home")],
}
LINES = {
    "worker": {"wake_up": "Up already? Me too.", "eat": "Lunch first, chat later.",
               "travel": "Off to work!", "work": "Work, work, work...",
               "socialize": "Long day. Nice evening.", "return_home": "Home time!",
               "sleep": "Zzz...", "night_warning": "Monsters roam at night!",
               "repeat": "You again? Hello!"},
    "wanderer": {"wake_up": "What a morning!", "wander": "Just stretching my legs.",
                 "eat": "Smells good, right?", "socialize": "Lovely weather today.",
                 "return_home": "Getting late, bye!", "sleep": "Zzz... mm, cake...",
                 "night_warning": "Careful in the dark!", "repeat": "Twice today!"},
    "early_bird": {"wake_up": "Up before the sun!", "patrol": "Keeping Potos safe.",
                   "eat": "Breakfast of heroes.", "work": "Busy, busy.",
                   "idle": "Just resting my feet.", "return_home": "Early to bed!",
                   "sleep": "Zzz...", "night_warning": "Stay safe out there.",
                   "repeat": "Back again?"},
}


def shift(time_text, minutes):
    hours, mins = (int(part) for part in time_text.split(":"))
    total = (hours * 60 + mins + minutes) % (24 * 60)
    return f"{total // 60:02d}:{total % 60:02d}"


def reachable_spots(grid, start, occupied):
    """[(tile, route length)] reachable from start within the route band."""
    spots = []
    for tile, open_ in grid.items():
        if not open_ or tile == start or tile in occupied:
            continue
        if max(abs(tile[0] - start[0]), abs(tile[1] - start[1])) > SEARCH:
            continue
        route = plan_route(grid, start, tile, occupied=occupied)
        if route is not None and MIN_ROUTE <= len(route) <= MAX_ROUTE:
            spots.append((tile, len(route)))
    return spots


def plan_villagers(wram, schedule):
    """(locations, npcs) to add for static Potos NPCs not yet scheduled."""
    known = {npc.get("native", {}).get("record") for npc in schedule.get("npcs", [])
             if npc.get("native", {}).get("map") == POTOS}
    npcs = [n for n in decode_npcs(wram) if n["static"] and n["record"] is not None]
    occupied = {n["tile"] for n in npcs}
    locations, added = {}, []
    for index, npc in enumerate(sorted(npcs, key=lambda n: n["record"])):
        if npc["record"] in known:
            continue
        x, y = npc["tile"]
        grid = walkable_grid(wram, npc["layer"], npc["floor"],
                             (x - SEARCH, y - SEARCH, x + SEARCH, y + SEARCH))
        if not grid.get((x, y)):
            continue                         # standing on something we cannot route from
        spots = reachable_spots(grid, (x, y), occupied - {(x, y)})
        if len(spots) < 2:
            continue
        work = max(spots, key=lambda spot: (spot[1], spot[0]))[0]
        meet = max(spots, key=lambda spot: (abs(spot[0][0] - work[0]) +
                                            abs(spot[0][1] - work[1]), spot[0]))[0]
        rec = npc["record"]
        prefix = f"potos_r{rec}"
        evidence = (f"live Turbo map 131: object record {rec} (kind {npc['kind']:#04x}, "
                    f"static) and reachable floor-{npc['floor']} route on the live work map")
        layer = npc["layer"]
        locations[f"{prefix}_home"] = {"map": POTOS, "x": x, "y": y, "layer": layer,
                                       "evidence": f"{evidence}; spawn tile"}
        locations[f"{prefix}_work"] = {"map": POTOS, "x": work[0], "y": work[1], "layer": layer,
                                       "evidence": evidence}
        locations[f"{prefix}_meet"] = {"map": POTOS, "x": meet[0], "y": meet[1], "layer": layer,
                                       "evidence": evidence}
        routine = list(ROUTINES)[index % len(ROUTINES)]
        offset = (rec * 17) % 50               # minutes: the town does not move in step
        alias = {"home": "home", "workplace": "workplace", "meeting": f"{prefix}_meet"}
        added.append({
            "id": f"potos_villager_r{rec}",
            "name": f"Potos villager (record {rec})",
            "home": f"{prefix}_home", "workplace": f"{prefix}_work",
            "native": {"map": POTOS, "record": rec, "kind": npc["kind"],
                       "event": npc["event"], "static": True, "spawn": f"{prefix}_home"},
            "personality": {"sociable": 0.4 + 0.1 * (rec % 5), "diligent": 0.5 + 0.1 * (rec % 4)},
            "schedule": [{"time": shift(time_text, offset), "activity": activity,
                          "destination": alias[where]}
                         for time_text, activity, where in ROUTINES[routine]],
            "lines": dict(LINES[routine]),
        })
    return locations, added


def merge(schedule, locations, npcs):
    merged = json.loads(json.dumps(schedule))
    merged.setdefault("locations", {}).update(locations)
    merged.setdefault("npcs", []).extend(npcs)
    return merged


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--host", default="root@100.124.74.13")
    parser.add_argument("--schedule", type=Path, default=DEFAULT_PATH)
    parser.add_argument("--write", action="store_true")
    args = parser.parse_args()
    from som_memory import WRAM
    from som_qwen_bridge import RemoteLink
    link = RemoteLink(args.host, partial=False)
    try:
        wram = WRAM(link.fetch_wram(False))
    finally:
        link.close()
    context = wram.tile_context()
    if wram.game_mode() != "field" or context.get("map_id") != POTOS:
        print(f"the hero must be in Potos (map {POTOS}, field mode); now map "
              f"{context.get('map_id')} mode {wram.game_mode()}", file=sys.stderr)
        return 2
    schedule = json.loads(args.schedule.read_text())
    locations, npcs = plan_villagers(wram, schedule)
    for npc in npcs:
        print(f"{npc['id']}: home {npc['home']} work {npc['workplace']}, "
              f"{len(npc['schedule'])} schedule entries")
    if not npcs:
        print("no new static Potos villagers found")
        return 0
    merged = merge(schedule, locations, npcs)
    World(merged)                                  # validates or raises ScheduleError
    if args.write:
        shutil.copy2(args.schedule, str(args.schedule) + ".bak")
        args.schedule.write_text(json.dumps(merged, indent=2) + "\n")
        print(f"added {len(npcs)} villagers to {args.schedule}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

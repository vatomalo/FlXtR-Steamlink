"""Where to hunt next: new places first, never the stale or too-easy ones.

The hero kept returning to the map with the best gold per HP lost, which is
the easiest one (map 16, 0.04 HP lost per decision at level 9), and it never
saw most of the world. This chooser treats the game as a graph of maps (ROM
exits plus crossings the hero made) and picks, in order:

1. the nearest map the hero has never stood on (a frontier), preferring maps
   that lead on (corridors, hubs) over dead-end rooms;
2. a known map that still teaches something: not too easy for the hero's
   current strength and not visited to exhaustion;
3. None, so the caller falls back to the old gold-per-HP choice.

"Too easy" is measured, not guessed: enemies there cost the hero less than
EASY_HP_FRACTION of max HP per decision over at least EASY_MIN_SAMPLES fight
decisions. A frontier the hero failed to reach FRONTIER_TRIES times (a story
gate, an unwalkable ROM exit) and maps where it died DEATH_LIMIT times are
skipped. Failures persist in agent_state "frontier".
"""
import sqlite3
from collections import deque

from som_game_over import deaths_by_map

MAX_HOPS = 12
EASY_MIN_SAMPLES = 1000
EASY_HP_FRACTION = 0.003
OVERVISITED = 6000            # tile visits + fight decisions
FRONTIER_TRIES = 2
DEATH_LIMIT = 2


def map_stats(history):
    """{map_id: {"visits", "fights", "taken"}} from verified memory."""
    stats = {}
    for map_id, visits in history.connection.execute(
            "SELECT map_id,SUM(visits) FROM tile_visits GROUP BY map_id"):
        stats.setdefault(map_id, {"visits": 0, "fights": 0, "taken": 0.0})["visits"] = visits or 0
    for map_id, fights, taken in history.connection.execute(
            "SELECT map_id,COUNT(*),AVG(damage_taken) FROM combat_samples GROUP BY map_id"):
        entry = stats.setdefault(map_id, {"visits": 0, "fights": 0, "taken": 0.0})
        entry["fights"], entry["taken"] = fights or 0, taken or 0.0
    return stats


def neighbours(history, map_id, rejected=frozenset()):
    found = set()
    tables = getattr(history, "rom_tables", None)
    if tables is not None and history.rom_exits_trusted():
        found |= tables.neighbours(map_id)
    for (to_map,) in history.connection.execute(
            "SELECT to_map FROM map_edges WHERE from_map=?", (map_id,)):
        if f"{map_id}>{to_map}" not in rejected:
            found.add(to_map)
    found.discard(map_id)
    return found


def too_easy(entry, max_hp):
    return bool(max_hp and entry["fights"] >= EASY_MIN_SAMPLES and
                entry["taken"] < EASY_HP_FRACTION * max_hp)


def overvisited(entry):
    return entry["visits"] + entry["fights"] >= OVERVISITED


class Frontier:
    def __init__(self, history, log=print):
        self.history, self.log = history, log
        self.last = None

    def _state(self):
        state = self.history.get_agent_state("frontier")
        return state if isinstance(state, dict) else {"failed": {}}

    def failed(self, map_id, why):
        """A trip toward map_id ended without arriving."""
        state = self._state()
        failed = state.setdefault("failed", {})
        failed[str(map_id)] = failed.get(str(map_id), 0) + 1
        self.history.set_agent_state("frontier", state)
        self.log(f"[frontier] map {map_id} not reached ({why}); "
                 f"try {failed[str(map_id)]}/{FRONTIER_TRIES}")

    def choose(self, here, max_hp, reachable=lambda map_id: True):
        """(map_id, reason) for the next hunt, or None (memory can't tell)."""
        if self.history is None or here is None:
            return None
        try:
            return self._choose(here, max_hp, reachable)
        except (sqlite3.Error, AttributeError) as exc:
            self.log(f"[frontier] unavailable ({exc}); best gold per HP instead")
            return None

    def _choose(self, here, max_hp, reachable):
        stats = map_stats(self.history)
        failed = self._state().get("failed", {})
        deaths = deaths_by_map(self.history.connection)
        rejected = frozenset(self.history.rejected_edges())

        def blocked(map_id):
            return (failed.get(str(map_id), 0) >= FRONTIER_TRIES or
                    deaths.get(map_id, 0) >= DEATH_LIMIT)

        hops = {here: 0}
        parents = {here: None}
        queue = deque([here])
        frontier, keep = [], []
        while queue:
            current = queue.popleft()
            if hops[current] >= MAX_HOPS:
                continue
            for nxt in sorted(neighbours(self.history, current, rejected)):
                if nxt in hops:
                    continue
                hops[nxt], parents[nxt] = hops[current] + 1, current
                entry = stats.get(nxt)
                if entry is None or entry["visits"] + entry["fights"] == 0:
                    if not blocked(nxt):
                        leads_on = len(neighbours(self.history, nxt, rejected) - {current})
                        # Corridors and hubs first; then the nearest.
                        frontier.append((0 if leads_on else 1, hops[nxt], nxt))
                    continue            # unknown ground: don't plan through it
                if not (too_easy(entry, max_hp) or overvisited(entry) or blocked(nxt)):
                    keep.append((entry["visits"] + entry["fights"], hops[nxt], nxt))
                queue.append(nxt)
        here_entry = stats.get(here)
        if here_entry and not (too_easy(here_entry, max_hp) or overvisited(here_entry)):
            keep.append((here_entry["visits"] + here_entry["fights"], 0, here))

        def route(map_id):
            path = [map_id]
            while parents[path[-1]] is not None:
                path.append(parents[path[-1]])
            return ">".join(str(m) for m in reversed(path))

        for _, _, map_id in sorted(frontier):
            if reachable(map_id):
                return self._pick(map_id, f"new place, {hops[map_id]} maps away via {route(map_id)}")
        for _, _, map_id in sorted(keep):
            if map_id == here or reachable(map_id):
                entry = stats[map_id]
                return self._pick(map_id, f"still worth it: {entry['fights']} fights, "
                                          f"{entry['taken']:.2f} HP lost per decision")
        return None

    def _pick(self, map_id, why):
        if map_id != self.last:
            self.log(f"[frontier] hunt map {map_id}: {why}")
            self.last = map_id
        return map_id, why

    @staticmethod
    def describe(history, map_id, max_hp):
        """Why a known map is skipped, or None."""
        entry = map_stats(history).get(map_id)
        if entry is None:
            return None
        if too_easy(entry, max_hp):
            return (f"too easy ({entry['taken']:.2f} HP lost per decision over "
                    f"{entry['fights']} fights)")
        if overvisited(entry):
            return f"visited {entry['visits'] + entry['fights']} times"
        return None

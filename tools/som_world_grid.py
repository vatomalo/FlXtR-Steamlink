"""One world grid for every map, measured in tiles, like an open-world game.

Open-world games (GTA III onwards, Spider-Man) keep one world coordinate
space split into fixed cells; every piece of the level knows where it sits
in it, so distance and direction between any two places are plain sums.
Secret of Mana has only separate maps joined by exits, so this module lays
them out on one grid from the ROM's own transition table (som_rom):

- A transition record (target map, x, y, flags) is listed in the event
  words of the map it leaves from. The arrival tile (x, y) in the target is
  next to the target's door back, so door[target][source] = (x, y).
- The low five bits of the flags are the walk the game plays on arrival
  (verified against crossings the hero made, 2026-11-21): 1 up, 2 down,
  3 right, 4 left. 5 walks into a house, 0 walks out of one; other codes
  are stairs, warps and cave mouths.
- Maps joined by a walk link are placed edge to edge: the arrival tile sits
  one step past the door it came through. Every other link (houses, caves,
  stairs) is a room: placed at its door so it has a heading, but walk
  links always win the placement.

Distances are measured, not hop counts: a route costs the tiles walked from
the door it enters a map by to the door it leaves by (Manhattan, the maps
are tile grids), plus CROSSING per map change. Headings compare the world
centres of the maps' doors. Separate islands (no link at all) get no
heading. Nothing here is a route to walk: the planner still walks only
verified tiles; this only measures and orders the way.
"""
import heapq
from collections import deque

CELL = 32                    # world grid cell, tiles
CROSSING = 4                 # a map change costs about this many tiles
UNKNOWN_DOOR = 32            # walk inside a map when a door tile is unknown
WALK = {1: (0, -1), 2: (0, 1), 3: (1, 0), 4: (-1, 0)}
COMPASS = ("east", "south-east", "south", "south-west",
           "west", "north-west", "north", "north-east")


class WorldGrid:
    def __init__(self, doors, codes):
        self.doors = doors            # {map: {neighbour: (x, y) door tile}}
        self.codes = codes            # {(from, to): low five flag bits}
        self.origin, self.island, self.room = {}, {}, set()
        self._place()

    @classmethod
    def from_rom(cls, tables, exit_rows=()):
        """From som_rom.RomTables; exit_rows (map, x, y, to_map) are trigger
        tiles scanned live, used where the ROM has no way back to measure by."""
        doors, codes = {}, {}
        for source, info in tables.maps.items():
            for event in info.get("events", ()):
                if not 0x800 <= event < 0xC00 or event - 0x800 >= len(tables.transitions):
                    continue
                target, x, y, _layer2, flags = tables.transitions[event - 0x800]
                if target == source:
                    continue
                doors.setdefault(target, {}).setdefault(source, (x, y))
                codes.setdefault((source, target), flags & 0x1F)
        for map_id, x, y, to_map in exit_rows:
            if to_map != map_id:
                doors.setdefault(map_id, {}).setdefault(to_map, (x, y))
        return cls(doors, codes)

    # Layout ------------------------------------------------------------------
    def _links(self, map_id):
        out = {n for (m, n) in self.codes if m == map_id}
        out |= {m for (m, n) in self.codes if n == map_id}
        return sorted(out)

    def _walk(self, a, b):
        """Step (dx, dy) of a walk link a->b, or None (a room link)."""
        code = self.codes.get((a, b))
        if code in WALK:
            return WALK[code]
        back = self.codes.get((b, a))
        if back in WALK:
            dx, dy = WALK[back]
            return (-dx, -dy)
        return None

    def _offset(self, a, b):
        door_a, door_b = self.doors.get(a, {}).get(b), self.doors.get(b, {}).get(a)
        if door_b is None:
            return None
        if door_a is None:
            # One-way (a script brings the hero back): the door in a is not
            # in the tables; its doors' middle stands in for it.
            known = list((self.doors.get(a) or {}).values())
            if not known:
                return None
            door_a = (sum(p[0] for p in known) // len(known),
                      sum(p[1] for p in known) // len(known))
        step = self._walk(a, b) or (0, 0)
        ax, ay = self.origin[a]
        return (ax + door_a[0] + step[0] - door_b[0], ay + door_a[1] + step[1] - door_b[1])

    def _place(self):
        maps = sorted(set(self.doors) | {m for pair in self.codes for m in pair})
        island = 0
        for seed in maps:
            if seed in self.origin:
                continue
            island += 1
            self.origin[seed], self.island[seed] = (0, 0), island
            # Walk links first (edge-to-edge geography), rooms only after:
            # a cave or a house never decides where a field map lies.
            walks, rooms = deque([seed]), deque()
            while walks or rooms:
                by_walk = bool(walks)
                here = walks.popleft() if by_walk else rooms.popleft()
                deferred = False
                for there in self._links(here):
                    if there in self.origin:
                        continue
                    walk = self._walk(here, there) is not None
                    if by_walk and not walk:
                        deferred = True       # rooms once every walk is placed
                        continue
                    placed = self._offset(here, there)
                    if placed is None:
                        continue
                    self.origin[there], self.island[there] = placed, island
                    if not walk:
                        self.room.add(there)
                    walks.append(there)
                if deferred:
                    rooms.append(here)

    # Queries -----------------------------------------------------------------
    def point(self, map_id, tile):
        """World tile of a map tile, or None for an unplaced map."""
        origin = self.origin.get(map_id)
        if origin is None or tile is None:
            return None
        return (origin[0] + tile[0], origin[1] + tile[1])

    def centre(self, map_id):
        """World centre of a map's doors (its own extent is not in the ROM tables)."""
        doors = self.doors.get(map_id) or {}
        points = [self.point(map_id, tile) for tile in doors.values()]
        points = [p for p in points if p is not None]
        if not points:
            return self.origin.get(map_id)
        return (sum(p[0] for p in points) / len(points), sum(p[1] for p in points) / len(points))

    def cell(self, map_id, tile=None):
        """(column, row) of the world grid cell a map tile (or the map) is in."""
        point = self.point(map_id, tile) if tile is not None else self.centre(map_id)
        if point is None:
            return None
        return (int(point[0] // CELL), int(point[1] // CELL))

    def heading(self, from_map, to_map, from_tile=None):
        """Compass word from one map (or tile) to another, None if unknown."""
        if self.island.get(from_map) is None or self.island.get(from_map) != self.island.get(to_map):
            return None
        start = self.point(from_map, from_tile) if from_tile is not None else self.centre(from_map)
        end = self.centre(to_map)
        if start is None or end is None:
            return None
        dx, dy = end[0] - start[0], end[1] - start[1]
        if abs(dx) < 1 and abs(dy) < 1:
            return None
        return compass(dx, dy)

    def toward(self, from_map, to_map, direction):
        """True when to_map lies broadly toward direction ("north", ...)."""
        heading = self.heading(from_map, to_map)
        return bool(heading and direction and direction in heading)

    def span(self, from_map, to_map):
        """Straight-line world tiles between two maps' centres, or None."""
        if self.island.get(from_map) is None or self.island.get(from_map) != self.island.get(to_map):
            return None
        a, b = self.centre(from_map), self.centre(to_map)
        if a is None or b is None:
            return None
        return abs(a[0] - b[0]) + abs(a[1] - b[1])

    def route(self, from_map, to_map, neighbours, start_tile=None, avoid=()):
        """(maps, tiles) of the shortest way measured in walked tiles, or None.

        neighbours(map) -> maps reachable from it (ROM exits, learned edges).
        Inside a map the walk is door-to-door Manhattan; a map entered from
        an unknown door costs UNKNOWN_DOOR."""
        if from_map == to_map:
            return [from_map], 0
        best = {(from_map, start_tile): 0}
        parents = {(from_map, start_tile): None}
        queue = [(0, 0, from_map, start_tile)]
        order = 1
        while queue:
            cost, _, here, entry = heapq.heappop(queue)
            if cost != best.get((here, entry)):
                continue
            if here == to_map:
                path, state = [], (here, entry)
                while state is not None:
                    path.append(state[0])
                    state = parents[state]
                return path[::-1], cost
            for there in sorted(neighbours(here)):
                if there in avoid or there == here:
                    continue
                door = self.doors.get(here, {}).get(there)
                inside = (abs(door[0] - entry[0]) + abs(door[1] - entry[1])
                          if door is not None and entry is not None else UNKNOWN_DOOR)
                arrival = self.doors.get(there, {}).get(here)
                state = (there, arrival)
                total = cost + inside + CROSSING
                if total < best.get(state, float("inf")):
                    best[state], parents[state] = total, (here, entry)
                    heapq.heappush(queue, (total, order, there, arrival))
                    order += 1
        return None

    def distances(self, from_map, neighbours, start_tile=None, limit=4000):
        """{map: walked tiles} of every map reachable from from_map."""
        best, found = {(from_map, start_tile): 0}, {from_map: 0}
        queue, order = [(0, 0, from_map, start_tile)], 1
        while queue:
            cost, _, here, entry = heapq.heappop(queue)
            if cost != best.get((here, entry)) or cost > limit:
                continue
            found.setdefault(here, cost)
            for there in sorted(neighbours(here)):
                if there == here:
                    continue
                door = self.doors.get(here, {}).get(there)
                inside = (abs(door[0] - entry[0]) + abs(door[1] - entry[1])
                          if door is not None and entry is not None else UNKNOWN_DOOR)
                state = (there, self.doors.get(there, {}).get(here))
                total = cost + inside + CROSSING
                if total < best.get(state, float("inf")):
                    best[state] = total
                    heapq.heappush(queue, (total, order, there, state[1]))
                    order += 1
        return found

    def describe(self, from_map, to_map):
        """'north-east, 140 tiles' style summary for logs ('' if unknown)."""
        heading, span = self.heading(from_map, to_map), self.span(from_map, to_map)
        if heading is None or span is None:
            return ""
        return f"{heading}, {span:.0f} tiles"


def compass(dx, dy):
    """Eight-way compass word for a world offset (y grows south)."""
    import math
    angle = math.degrees(math.atan2(dy, dx)) % 360
    return COMPASS[int((angle + 22.5) // 45) % 8]

"""Curiosity for the Secret of Mana AI: reward new places, penalise lingering.

The planner calls observe() once per field decision with the hero's verified
map, layer and tile. It returns events and keeps a running score:

- new map (first time ever on that map id): +NEW_MAP_REWARD
- new area (an 8x8-tile block of a map never stood in before): +NEW_AREA_REWARD
- lingering: LINGER_DECISIONS decisions in a row within LINGER_RADIUS tiles of
  one spot while nothing legitimate keeps the hero there (no enemy on
  screen, no errand such as a shop/inn trip, no recovery): -LINGER_PENALTY,
  then again only after another full stretch.

Rewards are paid once per map / area, so walking back and forth earns
nothing. The planner reacts to a linger event (frontier targets near the
spot are banned for a while and the heading changes) and prefers frontier
cells in areas that were never visited. State is a small JSON-able dict so
it survives restarts in the planner's agent state. No model calls, no I/O.
"""
from collections import deque

NEW_MAP_REWARD = 50
NEW_AREA_REWARD = 5
LINGER_PENALTY = 20
LINGER_DECISIONS = 60
LINGER_RADIUS = 2           # tiles (Chebyshev)
AREA_TILES = 8
MAX_AREAS = 4096            # bounded memory
HEADINGS = ("explore_north", "explore_east", "explore_south", "explore_west")


def area_of(map_id, layer, tile):
    return "%s:%s:%d:%d" % (map_id, layer, tile[0] // AREA_TILES, tile[1] // AREA_TILES)


class Curiosity:
    def __init__(self, state=None):
        state = state if isinstance(state, dict) else {}
        self.score = int(state.get("score", 0))
        self.maps = set(state.get("maps", []))
        self.areas = set(state.get("areas", [])[-MAX_AREAS:])
        self.lingers = int(state.get("lingers", 0))
        self.recent = deque(maxlen=LINGER_DECISIONS)
        self.cooldown = 0

    def to_state(self):
        return {"score": self.score, "maps": sorted(self.maps)[:MAX_AREAS],
                "areas": sorted(self.areas)[-MAX_AREAS:], "lingers": self.lingers}

    def area_seen(self, map_id, layer, tile):
        return area_of(map_id, layer, tile) in self.areas

    def observe(self, map_id, layer, tile, busy=False):
        """One field decision. Returns [(kind, points, note)]."""
        events = []
        if map_id not in self.maps:
            self.maps.add(map_id)
            self.score += NEW_MAP_REWARD
            events.append(("new_map", NEW_MAP_REWARD, "first visit to map %s" % map_id))
        area = area_of(map_id, layer, tile)
        if area not in self.areas and len(self.areas) < MAX_AREAS:
            self.areas.add(area)
            self.score += NEW_AREA_REWARD
            events.append(("new_area", NEW_AREA_REWARD, "new area %s" % area))
        if self.cooldown:
            self.cooldown -= 1
        if busy:
            self.recent.clear()            # fighting, errands: staying is fine
            return events
        self.recent.append((map_id, layer, tuple(tile)))
        if len(self.recent) == LINGER_DECISIONS and not self.cooldown:
            spots = list(self.recent)
            same_map = all(s[:2] == spots[0][:2] for s in spots)
            xs = [s[2][0] for s in spots]
            ys = [s[2][1] for s in spots]
            if same_map and max(xs) - min(xs) <= 2 * LINGER_RADIUS and \
                    max(ys) - min(ys) <= 2 * LINGER_RADIUS:
                self.score -= LINGER_PENALTY
                self.lingers += 1
                self.cooldown = LINGER_DECISIONS
                self.recent.clear()
                events.append(("linger", -LINGER_PENALTY,
                               "stayed within %d tiles of %d,%d for %d decisions" % (
                                   LINGER_RADIUS, xs[-1], ys[-1], LINGER_DECISIONS)))
        return events

    @staticmethod
    def escape_heading(current):
        """A different heading than the one that led here."""
        if current in HEADINGS:
            return HEADINGS[(HEADINGS.index(current) + 2) % 4]
        return "explore_unknown"

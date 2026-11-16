"""Persistent, intention-driven, fail-closed field planner for Secret of Mana."""
import math
import time
import heapq
import itertools
from collections import deque
from dataclasses import dataclass, replace

from som_curiosity import AREA_TILES, Curiosity, LINGER_RADIUS
from som_weapon_ranges import DEFAULT_REACH_PX, WEAPONS as RANGES, is_long
from som_build import profile as build_profile
from som_tactics import CLOSE_SWING_PX

from som_memory import detect_cuts, detect_whip_crossing, held_weapon, hero_airborne, is_whip_post

STEP_OFFSETS = {
    "step_right": (1, 0), "step_left": (-1, 0),
    "step_up": (0, -1), "step_down": (0, 1),
}
OPPOSITE_STEP = {
    "step_right": "step_left", "step_left": "step_right",
    "step_up": "step_down", "step_down": "step_up",
}

# Chase only monsters on screen (SNES view 256 x 224 around the hero).
ENGAGE_RANGE_X = 128
ENGAGE_RANGE_Y = 112
# Below this distance the straight-line step is used when no route is known.
CLOSE_QUARTERS_PX = 48
# Conservative initial sword/glove contact envelope until verified hit events
# establish actual per-weapon reach. Do not swing at 48x48 diagonally.
DEFAULT_STRIKE_AXIAL_PX = DEFAULT_REACH_PX
DEFAULT_STRIKE_SIDE_PX = 14
# Per-weapon reach and the short/long grouping live in som_weapon_ranges.
WEAPON_REACH_PX = {w.name: w.reach_px for w in RANGES}
WEAPON_MIN_PX = {w.name: w.min_px for w in RANGES if w.min_px}
# Exploration reinforcement (som_curiosity values): frontier areas are
# ranked in bands of VALUE_STEP points; a tile in a dead-end area costs up to
# DEAD_END_COST extra steps on a route.
VALUE_STEP = 5
DEAD_END_COST = 6
# Long-range weapons back away from an enemy closer than this, at most
# KITE_STEPS steps in a row before shooting anyway.
KEEP_DISTANCE_PX = 44
LINE_UP_MAX_PX = 40         # longest sidestep onto the enemy's line
KITE_STEPS = 4
# Decisions to ignore an enemy whose straight line and known routes are walled.
UNREACHABLE_COOLDOWN = 20
# Leave survival when the inn spot does not heal or no recovery happens.
# HP does not regenerate on its own, so an endless retreat is a dead end.
INN_WAIT_DECISIONS = 30
SURVIVAL_GIVE_UP_DECISIONS = 900
# Attacks on one grass tile before the planner stops trying to cut it.
CUT_ATTEMPTS = 3
# Whip crossings: swings from one tile and facing before giving it up for
# WHIP_BLOCK_DECISIONS: a learned crossing or a recognised post gets
# WHIP_ATTEMPTS, an untried ledge with floor beyond it one probe per run.
WHIP_ATTEMPTS = 3
WHIP_PROBE_ATTEMPTS = 1
WHIP_BLOCK_DECISIONS = 300
# After a failed set of swings, shift the stance this many times and swing
# again before giving the crossing up: map 30 tile 32,9 failed 3 swings
# from one pixel, gave up, and later failed from the same pixel again.
WHIP_REPOSITIONS = 2
WHIP_REACH_TILES = 5        # the whip box ends 86 px ahead (weapons-ranged.md 2)
WHIP_ROUTE_COST = 4         # weighted routes: a crossing costs a weapon switch
# A step chosen this many times without the hero moving is blocked for a while.
STALL_DECISIONS = 8
# Minimum time between swings. Turbo's recharge byte is unreliable; swings
# sent sooner after the last one dealt under half the damage (2026-11-14).
SWING_INTERVAL_S = 0.6
TARGET_SWITCH_PX = 24       # another enemy must be this much closer to switch
# Guard instead of swinging when the learned hit chance for this enemy type,
# distance and time since its last hit is at least ANTICIPATE_RISK, at most
# ANTICIPATE_MAX decisions in a row (then swing regardless).
ANTICIPATE_RISK = 0.35
ANTICIPATE_MIN_SAMPLES = 12
ANTICIPATE_MAX = 2
# A learned crossing this close to a native return door went through that door.
DOOR_CROSSING_TILES = 4
STALL_BLOCK_DECISIONS = 200
# A step that lands the hero on some other tile than the one it points at
# (a diagonal half-tile or slope sliding it sideways) this many times is
# blocked like a stall: live map 136 tiles 44,29/43,30 (16 Nov) bounced the
# hero between two tiles for minutes, moving every pulse so no stall fired.
DEFLECT_LIMIT = 2
# Crossings into a map whose exits all lead back before its door counts as a wall.
DEAD_END_VISITS = 3
# Learned slides are remembered across visits and restarts. Failsafe: this
# many field waits in a row (nothing left to do) and the remembered slides
# are ignored for SLIDE_FAILSAFE_DECISIONS, so a wrong memory cannot trap
# the hero; a remembered slide he then walks through cleanly is forgotten.
SLIDE_FAILSAFE_WAITS = 8
SLIDE_FAILSAFE_DECISIONS = 150
FRONTIER_NO_PROGRESS = 24
FRONTIER_COOLDOWN = 200
# Keep the current evasion heading unless another step is clearly safer.
EVADE_HYSTERESIS_PX = 16
# Facing byte E010 is trusted after this many steps agreed with it.
FACING_MIN_CONFIRMED = 3
# An enemy this far sideways still counts as in front when it is no farther
# sideways than ahead.
FRONT_SIDE_PX = 20
# Monsters whose attack goes out along their facing (som-decomp
# monster-commands.md swing chooser): Mushboom 0x02, Mushgloom 0x2A. The
# hero steps to their side instead of trading blows head-on, at most
# FLANK_STEPS steps in a row before swinging anyway.
FLANK_ENEMIES = frozenset((0x02, 0x2A))
FLANK_STEPS = 4
FLANK_LINE_PX = 12          # this close to its facing line counts as head-on
HEADING_MIN_PX = 3          # movement that sets a monster's heading
# Charged attacks: only against enemies with at least this much HP, and no
# higher than this stage (each stage takes about 1.5 s of holding).
CHARGE_MIN_ENEMY_HP = 40
CHARGE_MAX_STAGE = 2
# Two enemies within this box whose directions have a cosine below
# SPLIT_COSINE are "on opposite sides"; a step must raise it by SPLIT_GAIN.
SPLIT_RANGE_PX = 48
SPLIT_COSINE = -0.3
SPLIT_GAIN = 0.5
# Back away from an unprofitable enemy type closer than this (weapon reach).
AVOID_DISTANCE_PX = 64


PROVOKE_RANGE_PX = 96       # a hit with an enemy this close counts as provocation
PROVOKED_DECISIONS = 40     # how long the hero keeps fighting back after a hit

@dataclass(frozen=True)
class PlannerDecision:
    action: str
    reason: str
    intention: str = "observe"
    remaining_tiles: int | None = None
    # How many tiles the route continues in the direction of `action`. Long
    # held moves overshoot corners, so the bridge holds only on straight runs.
    straight_tiles: int | None = None
    # A tap that only turns the hero (toward an adjacent enemy or grass);
    # not moving is expected, so stall detection ignores it.
    turn_only: bool = False
    # For action "charge": the stage to hold the attack button until.
    charge_stage: int = 0
    enemy_slot: int | None = None
    navigation_path: tuple | None = None


class RouteMap(dict):
    """{position: (first action, distance)} plus how each was first reached."""

    def __init__(self, start):
        super().__init__({start: (None, 0)})
        self.start = start
        self.parents = {}

    def straight(self, target):
        """Tiles walked in the first direction before the route to target turns."""
        steps, position = [], target
        while position != self.start and position in self.parents:
            position, action = self.parents[position]
            steps.append(action)
        steps.reverse()
        run = 0
        for action in steps:
            if action != steps[0]:
                break
            run += 1
        return run


class MemoryPlanner:
    """Pursue a durable goal using verified WRAM and a learned map graph."""

    def __init__(self, history=None):
        self.history = history
        self.local_visits = {}
        self.local_cells = {}
        self.local_intention = None
        self.last_position = None
        self.local_walked = {}
        # Stall detection: the same step chosen while the hero's pixel
        # position does not change means the step is not possible here
        # (wrong layer under a bridge, an NPC, a mislearned tile...).
        self.stall = None
        self.blocked_moves = {}
        # Step deflections: (map, layer, x, y, step) -> count, and where the
        # last decision was made (map, layer, tile) to compare against.
        self.deflections = {}
        self.last_step_origin = None
        self.slides = None              # remembered slides, loaded lazily
        self.field_waits = 0
        self.slide_failsafe = 0
        # Grass cutting: learned cuttable attributes (without a database),
        # the previous observation to detect cuts, and attacks per tile.
        self.local_cuttable = set()
        self.previous_observation = None
        self.cut_attempts = {}
        self.previous_action = "none"
        self.local_signatures = set()
        # Enemy identities to avoid when no history database is used.
        self.local_avoided = set()
        # Facing byte cross-check: [steps agreeing, steps disagreeing].
        self.facing_checks = [0, 0]
        # Facing assumed from the last step sent while E010 is unverified.
        self.assumed_facing = None
        # Set by the bridge after charged attacks failed this run.
        self.charge_disabled = False
        # Set by the play loop when no learned route leads from this map to
        # the inn (boss arena, story pit): retreating is pointless, so the
        # planner fights on instead of evading at low HP.
        self.no_retreat = False
        # Optional som_block.BlockLearner: decides per enemy type whether
        # guarding with R pays off (learned from HP lost with/without it).
        self.block_learner = None
        # "When provoked, always fight": HP lost with an enemy close by sets
        # this many decisions of fighting back with no evading or retreat.
        self.provoked = 0
        self.last_hp = None
        # Play loop: every monster is a target; the "cost more HP than GP"
        # avoid list made the hero dance around enemies instead of hunting.
        self.fight_all = False
        # Set while the play loop hunts on the current map: exploration must
        # not walk over exit tiles (it used to leave map 129 for 128 and be
        # sent straight back, over and over).
        self.stay_on_map = False
        self.clock = time.monotonic
        self.last_swing = None
        # Optional som_tactics.TacticLearner: per enemy type fighting style.
        self.tactic_learner = None
        # som_build profile of the hero (set by the bridge from its stats).
        self.build, self.build_profile = "balanced", build_profile("balanced")
        # "Fight monsters" gambit off: ignore monsters unless they attack.
        self.peaceful = False
        # HP fraction at which the hero heads for the inn (gambit weights).
        self.retreat_fraction = 0.5
        # Optional som_anticipate.AttackPredictor: guard before likely hits.
        self.attack_predictor = None
        self.anticipated = 0
        self.kited_swing = None
        self.kite_steps = 0                  # long-range back-off steps in a row
        self.swing_decided_at = None
        self.just_hit = False
        self.previous_action = "none"
        self.combat_learner = None  # Optional, trained only from verified live attacks
        self.last_pixel = None
        self.block_cooldown = 0
        self.route_diagonals = {}
        self.route_threats = []
        self.enemy_tracks = {}       # slot -> (identity, x, y, heading)
        self.flank_steps = 0
        self.frontier_clock = 0
        self.frontier_rejected = {}
        # Curiosity (som_curiosity): rewards new maps/areas, penalises
        # lingering; loaded from agent state on the first field decision.
        self.curiosity = None
        self.escape_directive = None
        self.escape_left = 0
        # Whip crossings (whip posts over ledges): learned crossings without a
        # database, swings per (map, layer, x, y, step), spots given up for a
        # while, ledges already probed this run, and whether a whip is owned.
        self.local_whip = {}
        self.local_posts = set()
        self.whip_anchor = None
        self.whip_attempts = {}
        self.whip_repositions = {}
        self.whip_blocked = {}
        self.whip_probed = set()
        self.whip_ready = False

    @staticmethod
    def _open_steps(context):
        rows, radius = context.get("passability_rows"), context.get("radius")
        if not isinstance(rows, list) or not isinstance(radius, int):
            return []
        result = []
        for action, (dx, dy) in STEP_OFFSETS.items():
            row, column = radius + dy, radius + dx
            if (0 <= row < len(rows) and isinstance(rows[row], str) and
                    0 <= column < len(rows[row]) and rows[row][column] == "."):
                result.append(action)
        return result

    @staticmethod
    def _wrapped_delta(target, origin, extent):
        delta = target - origin
        if isinstance(extent, int) and extent > 0:
            delta = (delta + extent // 2) % extent - extent // 2
        return delta

    @staticmethod
    def _neighbor(position, offset, size):
        return ((position[0] + offset[0]) % size[0],
                (position[1] + offset[1]) % size[1])

    def _walked(self, map_id):
        """Tiles the hero has stood on: proven walkable, whatever the grid says."""
        if self.history is not None:
            return self.history.walked_cells(map_id)
        return self.local_walked.get(map_id, set())

    def _passable_steps(self, context):
        blocked = self._blocked_here(context)
        return [step for step in self._passable_steps_unfiltered(context)
                if step not in blocked]

    def _passable_steps_unfiltered(self, context):
        steps = self._open_steps(context)
        size = context.get("map_size_tiles")
        tile = context.get("actor_tile")
        if not (isinstance(size, list) and len(size) == 2 and
                isinstance(tile, list) and len(tile) == 2):
            return steps
        walked = self._walked(context.get("map_id"))
        # Grass grows back when the map reloads: a walked tile that now shows
        # known grass must be cut, not walked into (live map 129: the hero
        # circled beside regrown grass it had walked through before).
        regrown = {action for action, _ in self._cut_targets(context)}
        proven = (self.history.passable_signatures() if self.history is not None
                  else self.local_signatures)
        special = {tuple(cell["offset"]): cell
                   for cell in context.get("special_cells") or []
                   if isinstance(cell.get("offset"), list)}
        for action, offset in STEP_OFFSETS.items():
            if action in steps or action in regrown:
                continue
            cell = special.get(offset)
            # Proven by standing on it, or it looks (collision-wise) exactly
            # like a conditional tile the hero has already walked across.
            if (self._neighbor(tuple(tile), offset, tuple(size)) in walked or
                    (cell and cell.get("classification") == "conditional" and
                     cell.get("attributes") in proven)):
                steps.append(action)
        return steps

    def trusted_steps(self, observation):
        """Steps the planner may take beyond plain '.' tiles (for the guard)."""
        context = observation.get("tile_context") if isinstance(observation, dict) else None
        if not isinstance(context, dict) or context.get("game_mode") != "field":
            return set()
        return set(self._passable_steps(context)) | set(self._cut_steps(context))

    def _follow_taught_path(self, context, previous, directive):
        """Keep moving along walked tiles while the tile check is unavailable."""
        size, tile = context.get("map_size_tiles"), context.get("actor_tile")
        if not (context.get("game_mode") == "field" and not context.get("map_loading") and
                isinstance(size, list) and len(size) == 2 and
                isinstance(tile, list) and len(tile) == 2):
            return None
        walked = self._walked(context.get("map_id"))
        position = tuple(tile)
        if position not in walked:
            return None
        blocked = self._blocked_here(context)
        steps = [action for action, offset in STEP_OFFSETS.items()
                 if action not in blocked and
                 self._neighbor(position, offset, tuple(size)) in walked]
        if not steps:
            return None
        heading = {"explore_west": "step_left", "explore_east": "step_right",
                   "explore_north": "step_up", "explore_south": "step_down"}.get(directive)
        reverse = OPPOSITE_STEP.get(previous)
        for choice in (previous, heading):
            if choice in steps:
                return PlannerDecision(choice, "follow taught path across unverified tiles",
                                       "follow_taught_path")
        forward = [step for step in steps if step != reverse] or steps
        return PlannerDecision(forward[0], "follow taught path across unverified tiles",
                               "follow_taught_path")

    def _remember(self, observation, context):
        self.route_threats = [(e["x_px"]//16, e["y_px"]//16)
                             for e in observation.get("enemy_slots", []) if e.get("active") and
                             isinstance(e.get("x_px"), int) and isinstance(e.get("y_px"), int)]
        # A diagonal half tile is not a fully open 16x16 square. The old BFS
        # cut corners across its solid half and oscillated against the cliff.
        # Keep native passability truthful; penalize observed half tiles so
        # ordinary floor and verified cuts are preferred to tight corners.
        key = (context["map_id"], context["actor"]["layer"])
        diagonal = self.route_diagonals.setdefault(key, set())
        radius = context.get("radius", 0)
        center = context["actor_tile"]
        size = context["map_size_tiles"]
        for row_index, row in enumerate(context.get("attribute_rows") or []):
            for column, encoded in enumerate(row.split()):
                attributes = bytes.fromhex(encoded)
                if len(attributes) != 4:
                    continue
                position = ((center[0]+column-radius)%size[0], (center[1]+row_index-radius)%size[1])
                ground = attributes[2]
                if ground & 0xF0 == 0x10 and ground & 7 >= 4:
                    diagonal.add(position)
                else:
                    diagonal.discard(position)
        self._learn_whip(observation)
        if self.history is None:
            self.local_cuttable.update(detect_cuts(self.previous_observation, observation))
            self.previous_observation = observation
        if self.history is not None:
            self.history.learn_cuts(observation)
            self.history.remember_walk(observation)
            self.history.remember_position(observation)
            self.history.remember_world(observation)
            self.history.remember_recovery(observation)
            self.history.safe_anchor()
            return
        actor = context["actor"]
        self.local_walked.setdefault(context["map_id"], set()).add(tuple(context["actor_tile"]))
        for cell in context.get("special_cells") or []:
            if cell.get("offset") == [0, 0] and cell.get("classification") == "conditional":
                self.local_signatures.add(cell.get("attributes"))
        key = (context["map_id"], actor["layer"], *context["actor_tile"])
        if key != self.last_position:
            self.local_visits[key] = self.local_visits.get(key, 0) + 1
            self.last_position = key
        rows, radius = context["passability_rows"], context["radius"]
        width, height = context["map_size_tiles"]
        cx, cy = context["actor_tile"]
        kinds = {".": "open", "@": "open", "#": "blocked", "?": "conditional"}
        for row_index, row in enumerate(rows):
            for column, marker in enumerate(row):
                if marker in kinds:
                    position = ((cx + column - radius) % width,
                                (cy + row_index - radius) % height)
                    self.local_cells[(context["map_id"], actor["layer"], *position)] = kinds[marker]

    def _visits(self, map_id, layer, x, y):
        if self.history is not None:
            return self.history.visit_count(map_id, layer, x, y)
        return self.local_visits.get((map_id, layer, x, y), 0)

    def _known_cells(self, map_id, layer):
        if self.history is not None:
            cells = self.history.known_cells(map_id, layer)
        else:
            cells = {(x, y): kind for (known_map, known_layer, x, y), kind
                     in self.local_cells.items() if known_map == map_id and known_layer == layer}
        return cells

    def _get_intention(self):
        if self.history is not None:
            return self.history.get_agent_state("intention")
        return self.local_intention

    def _set_intention(self, intention):
        if self.history is not None:
            self.history.set_agent_state("intention", intention)
        else:
            self.local_intention = intention

    def _get_state(self, key):
        if self.history is not None:
            return self.history.get_agent_state(key)
        return getattr(self, "local_" + key, None)

    def _set_state(self, key, value):
        if self.history is not None:
            self.history.set_agent_state(key, value)
        else:
            setattr(self, "local_" + key, value)

    @staticmethod
    def _enter(kind, height):
        """Height after stepping onto a tile of this kind, or None if refused.

        The game's rule (som-decomp docs/maps.md 14.2): type 3 is a wall, a
        type-1 tile refuses height 2 and a type-2 tile refuses height 1; the
        height becomes the tile's type for 0-2 and is kept on pass-through
        (bridge deck) tiles. height None = unknown, so raised tiles are allowed.
        """
        if kind in ("open",):
            return 0
        if kind in ("pass", "cuttable"):
            return height
        if kind == "h1":
            return None if height == 2 else 1
        if kind == "h2":
            return None if height == 1 else 2
        return None if kind != "walked" else height

    @staticmethod
    def _routes(start, cells, size, height=None, weights=None, jumps=None, forbidden=None):
        """Shortest known paths as {position: (first action, distance)}.

        Searches (position, height) states so a bridge reached up a ramp is
        usable even though, seen from the river bank, its ends look like walls.
        jumps {position: [(step, landing, landing height)]} adds learned whip
        crossings; a route that starts with one has first action "whip:<step>".
        forbidden {(position, step)} are steps learned not to work (stalls,
        deflections), so routes go around them instead of into them.
        """
        if weights:
            routes = RouteMap(start)
            order = itertools.count()
            queue = [(0, next(order), start, height, None, 0, None)]
            costs = {(start, height): 0}
            while queue:
                cost, _, position, level, first, distance, parent = heapq.heappop(queue)
                if cost != costs[(position, level)]:
                    continue
                if position not in routes:
                    routes[position] = (first, distance)
                    routes.parents[position] = parent
                for action, neighbor, after, extra in MemoryPlanner._moves(
                        position, level, cells, size, jumps, forbidden):
                    total = cost + max(1, weights.get(neighbor, 1)) + extra
                    state = (neighbor, after)
                    if total >= costs.get(state, float("inf")):
                        continue
                    costs[state] = total
                    heapq.heappush(queue, (total, next(order), neighbor, after,
                        action if first is None else first, distance+1, (position, action)))
            return routes
        routes = RouteMap(start)
        seen = {(start, height)}
        queue = deque([(start, height, None, 0)])
        while queue:
            position, level, first, distance = queue.popleft()
            for action, neighbor, after, _ in MemoryPlanner._moves(
                    position, level, cells, size, jumps, forbidden):
                if (neighbor, after) in seen:
                    continue
                seen.add((neighbor, after))
                step = action if first is None else first
                if neighbor not in routes:
                    routes[neighbor] = (step, distance + 1)
                    routes.parents[neighbor] = (position, action)
                queue.append((neighbor, after, step, distance + 1))
        return routes

    @staticmethod
    def _moves(position, level, cells, size, jumps=None, forbidden=None):
        """(action, neighbour, height after, extra cost) of each move from here."""
        for action, offset in STEP_OFFSETS.items():
            if forbidden and (position, action) in forbidden:
                continue
            neighbor = MemoryPlanner._neighbor(position, offset, size)
            kind = cells.get(neighbor)
            if kind not in ("open", "pass", "cuttable", "h1", "h2"):
                continue
            after = MemoryPlanner._enter(kind, level)
            if after is None and kind in ("h1", "h2"):
                continue
            yield action, neighbor, after, 0
        for action, landing, height in (jumps or {}).get(position, ()):
            yield ("whip:" + action, landing, level if height is None else height,
                   WHIP_ROUTE_COST)

    def _choose_frontier(self, context, cells, routes, directive="explore_unknown",
                         open_steps=None):
        map_id, layer = context["map_id"], context["actor"]["layer"]
        size = tuple(context["map_size_tiles"])
        origin = tuple(context["actor_tile"])
        direction = directive.removeprefix("explore_")
        candidates = []
        for position, (first, distance) in routes.items():
            if first is None:
                continue
            if (open_steps is not None and first not in open_steps and
                    not first.startswith("whip:")):
                continue
            if self.frontier_rejected.get((map_id, layer, position), 0) > self.frontier_clock:
                continue
            unknown_edges = sum(
                self._neighbor(position, offset, size) not in cells
                for offset in STEP_OFFSETS.values())
            if not unknown_edges:
                continue
            visits = self._visits(map_id, layer, *position)
            dx = self._wrapped_delta(position[0], origin[0], size[0])
            dy = self._wrapped_delta(position[1], origin[1], size[1])
            direction_values = {
                "east": (dx > 0, dx, abs(dy)),
                "west": (dx < 0, -dx, abs(dy)),
                "south": (dy > 0, dy, abs(dx)),
                "north": (dy < 0, -dy, abs(dx)),
            }
            aligned, progress, straightness = direction_values.get(
                direction, (True, distance, 0))
            # A Qwen-selected heading is the first priority. Within that
            # heading, prefer unseen and distant frontier cells.
            # Curiosity: areas never stood in come before revisits.
            seen = bool(self.curiosity and self.curiosity.area_seen(map_id, layer, position))
            # Reinforcement: areas on the way to past discoveries first,
            # dead-end areas last (som_curiosity values, in VALUE_STEP bands
            # so distance still decides between similar areas).
            learned = (round(self.curiosity.value(map_id, layer, position) / VALUE_STEP)
                       if self.curiosity else 0)
            candidates.append((not aligned, seen, -learned, visits, -progress, straightness,
                               -distance, -unknown_edges, position))
        if not candidates:
            for position, (first, distance) in routes.items():
                if (distance and (open_steps is None or first in open_steps or
                                  first.startswith("whip:")) and
                        self.frontier_rejected.get((map_id, layer, position), 0)
                        <= self.frontier_clock):
                    candidates.append((False, False, 0, self._visits(map_id, layer, *position),
                                       -distance, 0, -distance, 0, position))
        return min(candidates)[-1] if candidates else None

    def _dead_end_weights(self, map_id, layer, cells):
        """Extra route cost through areas that led nowhere before (negative
        curiosity value): up to DEAD_END_COST more per tile. None when no
        area here is negative, so routes stay plain shortest paths."""
        if self.curiosity is None:
            return None
        prefix = "%s:%s:" % (map_id, layer)
        bad = {}
        for area, value in self.curiosity.values.items():
            if value < 0 and area.startswith(prefix):
                bx, by = area[len(prefix):].split(":")
                bad[(int(bx), int(by))] = 1 + min(DEAD_END_COST, int(-value // VALUE_STEP) + 1)
        if not bad:
            return None
        weights = {}
        for cell in cells:
            cost = bad.get((cell[0] // AREA_TILES, cell[1] // AREA_TILES))
            if cost:
                weights[cell] = cost
        return weights or None

    def _explore(self, context, open_steps, directive="explore_unknown"):
        self.frontier_clock += 1
        map_id, layer = context["map_id"], context["actor"]["layer"]
        position = tuple(context["actor_tile"])
        size = tuple(context["map_size_tiles"])
        cells = self._known_cells(map_id, layer)
        exits = set()
        if self.stay_on_map and hasattr(self.history, "exit_tiles"):
            exits = self.history.exit_tiles(map_id, layer)
        else:
            exits = self._dead_end_exits(map_id, layer)
        exits -= {position}
        if exits:
            # Walls, not unknown: removing them made every tile next to an
            # exit a frontier forever, and the hero walked between the
            # exits at both ends of the map (14 Nov).
            cells = dict(cells)
            cells.update({tile: "exit" for tile in exits})
        routes = self._routes(position, cells, size,
                              context["actor"].get("floor_height"),
                              self._dead_end_weights(map_id, layer, cells),
                              jumps=self._whip_jumps(context),
                              forbidden=self._forbidden_steps(context))
        if self.whip_ready:
            whip = self._post_approach(context, routes, open_steps)
            if whip is not None:
                return whip
        intention = self._get_intention()
        valid = (isinstance(intention, dict) and intention.get("kind") == "explore" and
                 intention.get("map_id") == map_id and intention.get("layer") == layer and
                 intention.get("directive") == directive and
                 isinstance(intention.get("target"), list))
        target = tuple(intention["target"]) if valid else None
        if (target == position or target not in routes or
                self.frontier_rejected.get((map_id, layer, target), 0) > self.frontier_clock):
            target = None
        if target is None:
            target = self._choose_frontier(context, cells, routes, directive,
                                           open_steps)
            if target is None:
                self._set_intention(None)
                # Nothing left to explore on foot: a whip post may lead on.
                options = self._whip_options(context)
                if options:
                    action = min(options)
                    return self._whip_cross(context, action, options[action])
                return None
            intention = {"kind": "explore", "map_id": map_id, "layer": layer,
                         "directive": directive,
                         "target": list(target), "last_position": list(position),
                         "stagnant": 0, "best_distance": routes[target][1],
                         "no_progress": 0}
        else:
            last = tuple(intention.get("last_position", []))
            intention["stagnant"] = intention.get("stagnant", 0) + 1 if last == position else 0
            intention["last_position"] = list(position)
            distance_now = routes[target][1]
            best = intention.get("best_distance", distance_now)
            if distance_now < best:
                intention["best_distance"] = distance_now
                intention["no_progress"] = 0
            else:
                intention["no_progress"] = intention.get("no_progress", 0) + 1
            if (intention["stagnant"] >= 8 or
                    intention["no_progress"] >= FRONTIER_NO_PROGRESS):
                self.frontier_rejected[(map_id, layer, target)] = (
                    self.frontier_clock + FRONTIER_COOLDOWN)
                if self.curiosity is not None:
                    self.curiosity.punish(map_id, layer, target)
                    self._set_state("curiosity", self.curiosity.to_state())
                self._set_intention(None)
                return None
        action, distance = routes[target]
        if action.startswith("whip:"):
            self._set_intention(intention)
            return self._whip_cross(context, action[5:], f"learned crossing toward "
                                    f"frontier {target[0]},{target[1]}")
        if action not in open_steps:
            if action in self._cut_steps(context):
                self._set_intention(intention)
                return self._cut(context, action)
            self._set_intention(None)
            return None
        self._set_intention(intention)
        return PlannerDecision(action,
                               f"frontier {target[0]},{target[1]} is {distance} known tiles away",
                               "explore_frontier", distance, routes.straight(target))

    def _combat(self, observation, context, open_steps, directive=None):
        actor = context["actor"]
        dimensions = context.get("map_size_tiles", [0, 0])
        width_px, height_px = dimensions[0] * 16, dimensions[1] * 16
        enemies = [enemy for enemy in observation.get("enemy_slots", []) if enemy.get("active")]
        if not enemies:
            # Ordinary monster slots can disappear for a few samples during
            # phase updates or at the screen edge. Preserve the last verified
            # target briefly instead of alternating combat and exploration.
            intention = self._get_intention()
            if isinstance(intention, dict) and intention.get("kind") == "engage":
                misses = int(intention.get("misses", 0)) + 1
                target_x, target_y = intention.get("x_px"), intention.get("y_px")
                if misses <= 4 and isinstance(target_x, int) and isinstance(target_y, int):
                    intention["misses"] = misses
                    self._set_intention(intention)
                    dx = self._wrapped_delta(target_x, actor["x_px"], width_px)
                    dy = self._wrapped_delta(target_y, actor["y_px"], height_px)
                    action = self._approach_direction(dx, dy, open_steps)
                    if action:
                        return PlannerDecision(
                            action, f"pursue last-known enemy position ({misses}/4 missing samples)",
                            "engage_enemy")
                    return PlannerDecision("wait", "hold last-known enemy area", "engage_enemy")
            return None
        if self.peaceful and not self.provoked:
            return None
        cooldowns = self._get_state("unreachable") or {}
        if not isinstance(cooldowns, dict):
            cooldowns = {}
        cooldowns = {slot: turns - 1 for slot, turns in cooldowns.items() if turns > 1}
        self._set_state("unreachable", cooldowns or None)
        enemies = [enemy for enemy in enemies if str(enemy.get("slot")) not in cooldowns]
        # Reward ledger: types that cost far more HP than the GP they pay
        # are not engaged, and the hero backs away when one comes close.
        avoided = set() if (self.provoked or self.fight_all) else self._avoided_identities()
        shunned = [enemy for enemy in enemies if enemy.get("identity") in avoided]
        enemies = [enemy for enemy in enemies if enemy.get("identity") not in avoided]
        close = []
        for enemy in shunned:
            dx = self._wrapped_delta(enemy["x_px"], actor["x_px"], width_px)
            dy = self._wrapped_delta(enemy["y_px"], actor["y_px"], height_px)
            if abs(dx) + abs(dy) <= AVOID_DISTANCE_PX:
                close.append((abs(dx) + abs(dy), dx, dy, enemy))
        if close and open_steps:
            _, dx, dy, enemy = min(close, key=lambda value: value[0])
            # Straight away from it (squared distance; Manhattan ties sideways).
            action = max(open_steps, key=lambda step: (
                (dx - STEP_OFFSETS[step][0] * 16) ** 2 +
                (dy - STEP_OFFSETS[step][1] * 16) ** 2, step))
            return PlannerDecision(action, f"back away from enemy type "
                                   f"{enemy['identity']:#04x}: it cost more HP than GP",
                                   "evade_enemy")
        targets = []
        for enemy in enemies:
            dx = self._wrapped_delta(enemy["x_px"], actor["x_px"], width_px)
            dy = self._wrapped_delta(enemy["y_px"], actor["y_px"], height_px)
            # Only monsters on screen are worth chasing; one 380 px away
            # behind a wall made the hero walk back and forth for minutes.
            if abs(dx) <= ENGAGE_RANGE_X and abs(dy) <= ENGAGE_RANGE_Y:
                targets.append((abs(dx) + abs(dy), dx, dy, enemy))
        if not targets:
            return None
        distance, dx, dy, enemy = min(targets, key=lambda value: value[0])
        # Keep the current target unless another is clearly closer: two
        # monsters at 70 and 71 px made the hero turn back and forth.
        engaged = self._get_intention()
        if isinstance(engaged, dict) and engaged.get("kind") == "engage":
            kept = next((value for value in targets
                         if value[3].get("slot") == engaged.get("enemy")), None)
            if kept is not None and kept[0] <= distance + TARGET_SWITCH_PX:
                distance, dx, dy, enemy = kept
        hero = observation.get("party", [{}])[0]
        split = self._unsplit([value[3] for value in targets], actor,
                              (width_px, height_px), open_steps)
        if split is not None and not (hero.get("max_hp") and
                                      hero.get("hp", 0) * 4 <= hero["max_hp"]):
            return split
        hp, max_hp = hero.get("hp", 0), hero.get("max_hp", 0)
        critical_hp = bool(max_hp and hp * 4 <= max_hp)
        # Fighting back never outranks escaping at critical HP: provoked
        # fight-back with no Candy left got the hero killed (14 Nov).
        if (critical_hp and (not self.provoked or not self.no_retreat)) or \
                (not self.provoked and directive == "avoid_enemy"):
            retreat = []
            for action in open_steps:
                sx, sy = STEP_OFFSETS[action]
                retreat.append((abs(dx - sx * 16) + abs(dy - sy * 16), action))
            if retreat:
                action = max(retreat)[1]
                self._set_intention({"kind": "survive", "enemy": enemy["slot"]})
                return PlannerDecision(action, f"retreat from enemy {enemy['slot']} at low HP",
                                       "survive")
        self._set_intention({"kind": "engage", "enemy": enemy["slot"],
                             "x_px": enemy["x_px"], "y_px": enemy["y_px"],
                             "misses": 0})
        recharge = hero.get("weapon_recharge")
        # Long range: keep the enemy at arm's length. Shoot when lined up and
        # ready (handled below); otherwise step away while it is inside
        # KEEP_DISTANCE_PX, at most KITE_STEPS in a row so a hero cornered
        # or chased down still fights back.
        held_now = (hero.get("weapon") or {}).get("type")
        if is_long(held_now) and observation.get("weapons_enabled"):
            kite = self._keep_distance(enemy, held_now, dx, dy, distance, recharge,
                                       open_steps)
            if kite is not None:
                return kite
        else:
            self.kite_steps = 0
        # Turbo's R guard blocks physical hits. Defend briefly when the
        # weapon is still recharging and an enemy is already in striking
        # distance. Cooldown prevents standing still blocking forever.
        if self.block_learner is not None:
            # Turbo's recharge byte often reads 0 in fights, so also guard in
            # the recovery window right after a swing and when just hit.
            exposed = (self.previous_action in ("attack", "charge") or self.just_hit or
                       (recharge is not None and recharge > 0))
            wants_block = (exposed and distance <= 40 and
                           self.block_learner.should_block(enemy.get("identity")))
        else:
            wants_block = (recharge is not None and recharge >= 50 and distance <= 32 and
                           max_hp and hp * 4 <= max_hp * 3)
        proven = (wants_block and hasattr(self.block_learner, "proven") and
                  self.block_learner.proven(enemy.get("identity")))
        if wants_block and (self.block_cooldown == 0 or proven):
            # Once blocking has measurably paid against this enemy, guard on
            # every exposed decision (swing recovery, just hit, recharging):
            # strike, guard, strike. While still exploring, keep the cooldown
            # so unblocked samples keep coming in.
            self.block_cooldown = (0 if proven else
                                   4 if self.block_learner is not None else 10)
            return PlannerDecision("block",
                                   f"guard against enemy {enemy['slot']} while weapon recharges",
                                   "defend_enemy")

        # A destructible obstacle between the hero and a visible enemy
        # must be resolved before pursuit. Otherwise routing around the
        # obstacle can alternate left/right forever as the enemy moves.
        # Only attempt verified, previously learned grass adjacent to the
        # hero. Never confuse a wall or unknown collision with vegetation.
        if observation.get("weapons_enabled") and recharge == 0:
            cutting = self._grass_blocking_enemy(context, dx, dy)
            if cutting is not None:
                action, reach = cutting
                if reach == 1:
                    return self._cut(context, action)
                if action in open_steps:
                    return PlannerDecision(action, f"step up to grass blocking enemy "
                                           f"{enemy['slot']}", "cut_grass")

        # Actual melee reach is much shorter than the old 48x48 test.
        # Require a forward axial distance and narrow lateral alignment.
        # The range learner may refine approach choices after valid hits.
        held = (hero.get("weapon") or {}).get("type")
        reach = WEAPON_REACH_PX.get(held, DEFAULT_STRIKE_AXIAL_PX)
        nearest = WEAPON_MIN_PX.get(held, 0)
        attack_aligned = ((nearest < abs(dx) <= reach and
                           abs(dy) <= DEFAULT_STRIKE_SIDE_PX) or
                          (nearest < abs(dy) <= reach and
                           abs(dx) <= DEFAULT_STRIKE_SIDE_PX))
        if attack_aligned and not is_long(held) and self._tactic(enemy) == "close":
            attack_aligned = abs(dx) + abs(dy) <= CLOSE_SWING_PX
        if attack_aligned and observation.get("weapons_enabled"):
            # Only a mature learned range can veto a normal swing. Existing
            # attack/charge logic remains the safe fallback for sparse data.
            weapon = hero.get("weapon") or {}
            learner = self.combat_learner
            alone = (len([e for e in observation.get("enemy_slots", [])
                          if e.get("active")]) == 1 and
                     not any(p.get("availability_byte") == 1
                             for p in (observation.get("party") or [])[1:]))
            # Counter: right after holding the guard, strike at once (no
            # charge, no range-learner veto) while the enemy is still close.
            countering = self.previous_action == "block" and recharge == 0
            close_in = (not countering and learner is not None and alone and
                        not is_long(held) and
                        weapon.get("verified") and
                        recharge == 0 and not self.charge_stage(hero, enemy) and
                        learner.should_close(
                            enemy.get("identity"), weapon.get("type"), distance,
                            hp / max_hp if max_hp else 1.0))
            if not close_in:
                strike = self._face_and_strike(context, enemy, dx, dy, open_steps,
                                               recharge == 0, None if countering else hero)
                if strike is not None:
                    if countering and strike.action == "attack":
                        return PlannerDecision("attack", f"counter enemy {enemy['slot']} "
                                               "after guarding", "engage_enemy")
                    return strike
        # Long range (bow, javelin, boomerang, whip): within the weapon's reach
        # on one axis, step sideways onto the enemy's line and shoot from
        # there; do not walk up to it.
        if is_long(held) and observation.get("weapons_enabled"):
            lined = self._line_up(enemy, held, dx, dy, reach, nearest, open_steps)
            if lined is not None:
                return lined
        # Follow the learned map first. Taking the straight-line step whenever
        # its first tile was open, and the map route only when it was not,
        # sent the hero up into a dead end and back down, over and over.
        routed = self._route_toward(context, dx, dy, open_steps)
        if routed:
            action, tiles, straight = routed
            return PlannerDecision(action, f"close on enemy {enemy['slot']} at {distance}px "
                                   f"({tiles} known tiles)", "engage_enemy", tiles, straight)
        action = self._approach_direction(dx, dy, open_steps)
        if action and distance <= CLOSE_QUARTERS_PX:
            return PlannerDecision(action, f"close on enemy {enemy['slot']} at {distance}px",
                                   "engage_enemy")
        cooldowns[str(enemy["slot"])] = UNREACHABLE_COOLDOWN
        self._set_state("unreachable", cooldowns)
        self._set_intention(None)
        return PlannerDecision("wait", f"enemy {enemy['slot']} unreachable; ignoring it for "
                               f"{UNREACHABLE_COOLDOWN} decisions", "engage_enemy")

    @staticmethod
    def _toward(dx, dy):
        """Facing that points at an offset; the horizontal axis wins ties,
        as the game's own diagonal facing does."""
        if abs(dx) >= abs(dy):
            return "step_right" if dx > 0 else "step_left"
        return "step_down" if dy > 0 else "step_up"

    def _learn_facing(self, context):
        """Cross-check the facing byte: a step that moved the hero that way
        must leave the hero facing that way (Turbo layouts are verified)."""
        actor = context.get("actor") or {}
        here = (context.get("map_id"), actor.get("x_px"), actor.get("y_px"))
        last, self.last_pixel = self.last_pixel, here
        step = self.previous_action
        if (step not in STEP_OFFSETS or last is None or last[0] != here[0] or
                not isinstance(here[1], int) or not isinstance(last[1], int)):
            return
        sx, sy = STEP_OFFSETS[step]
        moved = (here[1] - last[1]) * sx + (here[2] - last[2]) * sy
        sideways = abs((here[1] - last[1]) * sy) + abs((here[2] - last[2]) * sx)
        if moved < 2 or sideways > moved or actor.get("facing") is None:
            return
        self.facing_checks[0 if actor["facing"] == step else 1] += 1

    def facing_trusted(self):
        confirmed, contradicted = self.facing_checks
        return confirmed >= FACING_MIN_CONFIRMED and contradicted * 10 <= confirmed

    def charge_stage(self, hero, enemy):
        """Stage worth charging to before this swing, or 0 for a plain swing.

        Only with a verified weapon of level >= 1, above half HP (a hit cuts
        the charge), against an enemy with enough HP left to make the wait
        worth it, and while charging has not failed this run.
        """
        weapon = (hero or {}).get("weapon") or {}
        hp, max_hp = (hero or {}).get("hp", 0), (hero or {}).get("max_hp", 0)
        if (self.charge_disabled or not weapon.get("verified") or
                not weapon.get("level") or not max_hp or hp * 2 <= max_hp or
                not enemy.get("hp_plausible", True) or
                enemy.get("hp", 0) < CHARGE_MIN_ENEMY_HP):
            return 0
        return min(int(weapon["level"]), CHARGE_MAX_STAGE)

    def _keep_distance(self, enemy, held, dx, dy, distance, recharge, open_steps):
        """A step away from an enemy too close for a long-range weapon, or
        None (far enough, a ready shot lined up, or KITE_STEPS used up)."""
        if distance >= KEEP_DISTANCE_PX:
            self.kite_steps = 0
            return None
        reach, nearest = WEAPON_REACH_PX.get(held, DEFAULT_STRIKE_AXIAL_PX), WEAPON_MIN_PX.get(held, 0)
        lined = ((nearest < abs(dx) <= reach and abs(dy) <= DEFAULT_STRIKE_SIDE_PX) or
                 (nearest < abs(dy) <= reach and abs(dx) <= DEFAULT_STRIKE_SIDE_PX))
        ready = recharge in (None, 0) and self.previous_action not in ("attack", "charge")
        if lined and ready:
            self.kite_steps = 0              # shoot now; back off again after
            return None
        if self.kite_steps >= KITE_STEPS:
            return None                      # chased down: fight from here
        # Straight away on the enemy's axis first, then sideways away from it.
        options = []
        for action in open_steps:
            sx, sy = STEP_OFFSETS[action]
            gain = (dx - sx * 16) ** 2 + (dy - sy * 16) ** 2 - (dx * dx + dy * dy)
            if gain > 0:
                options.append((gain, action))
        if not options:
            return None                      # cornered: fight
        self.kite_steps += 1
        action = max(options)[1]
        return PlannerDecision(action, f"keep distance from enemy {enemy['slot']} "
                               f"({distance} px, {held} wants {KEEP_DISTANCE_PX}+)",
                               "kite_enemy")

    @staticmethod
    def _line_up(enemy, held, dx, dy, reach, nearest, open_steps):
        """A sideways step that puts a ranged shot on the enemy's line, or None.

        Shooting along x needs the enemy within (nearest, reach] px on x and
        the y offset brought inside DEFAULT_STRIKE_SIDE_PX; the same for y.
        The axis needing the smaller sideways move wins; moves longer than
        LINE_UP_MAX_PX are left to routing."""
        options = []
        if nearest < abs(dx) <= reach:
            options.append((abs(dy), "step_down" if dy > 0 else "step_up", abs(dx)))
        if nearest < abs(dy) <= reach:
            options.append((abs(dx), "step_right" if dx > 0 else "step_left", abs(dy)))
        for side, step, along in sorted(options):
            if side <= DEFAULT_STRIKE_SIDE_PX:
                return None              # already lined up: strike or turn
            if side > LINE_UP_MAX_PX:
                # Far off the line is not a sidestep but a walk; leave it to
                # routing. Sliding 111 px sideways fought the route around a
                # wall, left/right for minutes (2026-11-16).
                continue
            if step in open_steps:
                return PlannerDecision(step, f"line up the {held} on enemy {enemy['slot']} "
                                       f"({along} px away, {side} px off the line)",
                                       "engage_enemy")
        return None

    def _tactic(self, enemy):
        if self.tactic_learner is None:
            return self.build_profile.get("preferred") or "press"
        return self.tactic_learner.tactic(enemy.get("identity"),
                                          self.build_profile.get("preferred"),
                                          self.build_profile.get("tactics"))

    def _face_and_strike(self, context, enemy, dx, dy, open_steps, ready, hero=None):
        """Attack only while facing the enemy; otherwise tap toward it first.

        With an unverified facing byte the hero always taps toward the enemy
        on the decision before a swing (like facing grass before cutting).
        """
        want = self._toward(dx, dy)
        flank = self._flank(enemy, dx, dy, open_steps)
        if flank is not None:
            return flank
        facing = ((context.get("actor") or {}).get("facing") if self.facing_trusted()
                  else self.assumed_facing)
        if self._in_front(facing, dx, dy):
            stage = self.charge_stage(hero, enemy) if ready else 0
            if stage:
                return PlannerDecision("charge", f"charge stage {stage} at enemy "
                                       f"{enemy['slot']} ({enemy.get('hp')} HP), facing it",
                                       "charge_attack", charge_stage=stage,
                                       enemy_slot=enemy.get("slot"))
            if ready:
                since = (self.clock() - self.last_swing) if self.last_swing is not None else None
                if since is not None and since < SWING_INTERVAL_S:
                    # A swing before the gauge refills does little damage
                    # (live: 1.6 vs 3.6 per swing). Hold the guard instead,
                    # or with the kite tactic step out of reach; the next
                    # swing is the counter.
                    if self._tactic(enemy) == "kite" and self.kited_swing != self.last_swing:
                        away = OPPOSITE_STEP.get(want)
                        if away in open_steps:
                            # One step back per swing: more looked like
                            # walking back and forth.
                            self.kited_swing = self.last_swing
                            return PlannerDecision(away, f"step back from enemy {enemy['slot']} "
                                                   "while the weapon recharges", "kite_enemy")
                    return PlannerDecision("block", f"guard while the weapon recharges "
                                           f"({since:.1f}s since swing)", "defend_enemy")
                if self.attack_predictor is not None and self.anticipated < ANTICIPATE_MAX:
                    risk, samples = self.attack_predictor.risk(enemy.get("identity"),
                                                               abs(dx) + abs(dy))
                    threshold = self.build_profile.get("anticipate_risk", ANTICIPATE_RISK)
                    if samples >= ANTICIPATE_MIN_SAMPLES and risk >= threshold:
                        self.anticipated += 1
                        away = OPPOSITE_STEP.get(want)
                        if self.build_profile.get("dodge") and away in open_steps:
                            return PlannerDecision(away, f"sidestep enemy {enemy['slot']}'s "
                                                   f"attack ({risk:.0%} hit chance now)",
                                                   "kite_enemy")
                        return PlannerDecision("block", f"anticipate enemy {enemy['slot']}'s "
                                               f"attack ({risk:.0%} hit chance now)",
                                               "defend_enemy")
                self.anticipated = 0
                self.swing_decided_at = self.clock()
                return PlannerDecision("attack", f"enemy {enemy['slot']} in weapon range, "
                                       f"facing it", "engage_enemy")
            return None
        if want in open_steps:
            return PlannerDecision(want, f"turn to face enemy {enemy['slot']} before "
                                   "attacking", "engage_enemy", turn_only=True)
        # Cannot turn that way through a wall: try the other axis toward it.
        other = ("step_down" if dy > 0 else "step_up") if want in ("step_left", "step_right") \
            else ("step_right" if dx > 0 else "step_left")
        if other in open_steps and abs(dy if other in ("step_up", "step_down") else dx) >= 8:
            return PlannerDecision(other, f"turn toward enemy {enemy['slot']} (wall "
                                   "on the direct side)", "engage_enemy", turn_only=True)
        # An unverified facing direction is not a reason to swing blindly.
        # Close distance or find a valid turning tile instead.
        return None

    def _enemy_heading(self, enemy):
        """The way a monster faces, from how it last moved (monsters face
        where they walk); None until it has moved."""
        slot, x, y = enemy.get("slot"), enemy.get("x_px"), enemy.get("y_px")
        if not isinstance(x, int) or not isinstance(y, int):
            return None
        old = self.enemy_tracks.get(slot)
        heading = None
        if old is not None and old[0] == enemy.get("identity"):
            heading = old[3]
            mx, my = x - old[1], y - old[2]
            if max(abs(mx), abs(my)) >= HEADING_MIN_PX:
                heading = (("step_right" if mx > 0 else "step_left") if abs(mx) >= abs(my)
                           else ("step_down" if my > 0 else "step_up"))
            elif heading is not None:
                x, y = old[1], old[2]        # keep measuring from where it was
        self.enemy_tracks[slot] = (enemy.get("identity"), x, y, heading)
        return heading

    def _flank(self, enemy, dx, dy, open_steps):
        """Step out of a head-on monster's line toward its side, or None.

        dx, dy: enemy minus hero. Only FLANK_ENEMIES, only while the hero
        stands in front of the way it faces, FLANK_STEPS at most in a row.
        """
        heading = self._enemy_heading(enemy)
        if enemy.get("identity") not in FLANK_ENEMIES or heading is None:
            self.flank_steps = 0
            return None
        fx, fy = STEP_OFFSETS[heading]
        along = -dx * fx - dy * fy               # hero ahead of the monster's face
        side = -dx if fy else -dy                # hero's offset across its line
        if along <= 0 or abs(side) > FLANK_LINE_PX or self.flank_steps >= FLANK_STEPS:
            if along <= 0 or abs(side) > FLANK_LINE_PX:
                self.flank_steps = 0
            return None
        if fx:   # it faces left/right: get above or below its line
            options = ("step_down", "step_up") if side >= 0 else ("step_up", "step_down")
        else:
            options = ("step_right", "step_left") if side >= 0 else ("step_left", "step_right")
        for step in options:
            if step in open_steps:
                self.flank_steps += 1
                return PlannerDecision(step, f"enemy {enemy['slot']} faces us head-on: "
                                       f"stepping {step[5:]} to its side "
                                       f"({self.flank_steps}/{FLANK_STEPS})", "flank_enemy")
        return None

    @staticmethod
    def _in_front(facing, dx, dy):
        """Enemy ahead of the facing and not farther sideways than ahead."""
        if facing not in STEP_OFFSETS:
            return False
        fx, fy = STEP_OFFSETS[facing]
        along, side = dx * fx + dy * fy, abs(dx * fy) + abs(dy * fx)
        return along > 0 and side <= max(along, FRONT_SIDE_PX)

    def _unsplit(self, enemies, actor, size_px, open_steps):
        """Step out from between two enemies on opposite sides, or None.

        Fighting one with the other behind lets the second hit the hero's
        back on every swing (user report). The open step that puts both on
        one side (largest cosine between them afterwards) is taken when it
        clearly helps and keeps a little room to each.
        """
        near = []
        for enemy in enemies:
            dx = self._wrapped_delta(enemy["x_px"], actor["x_px"], size_px[0])
            dy = self._wrapped_delta(enemy["y_px"], actor["y_px"], size_px[1])
            if abs(dx) <= SPLIT_RANGE_PX and abs(dy) <= SPLIT_RANGE_PX:
                near.append((abs(dx) + abs(dy), dx, dy, enemy))
        if len(near) < 2:
            return None
        near.sort(key=lambda value: value[0])

        def spread(offsets):
            (ax, ay), (bx, by) = offsets
            norm = math.hypot(ax, ay) * math.hypot(bx, by)
            return (ax * bx + ay * by) / norm if norm else -1.0

        pair = [(dx, dy) for _, dx, dy, _ in near[:2]]
        now = spread(pair)
        if now > SPLIT_COSINE:
            return None
        best = None
        for step in open_steps:
            sx, sy = STEP_OFFSETS[step]
            moved = [(dx - sx * 16, dy - sy * 16) for dx, dy in pair]
            if min(abs(x) + abs(y) for x, y in moved) < 12:
                continue
            score = spread(moved)
            if score >= now + SPLIT_GAIN and (best is None or score > best[0]):
                best = (score, step)
        if best is None:
            return None
        slots = " and ".join(str(enemy["slot"]) for *_, enemy in near[:2])
        return PlannerDecision(best[1], f"enemies {slots} on opposite sides; stepping out "
                               "to fight both from one side", "engage_enemy")

    def _avoided_identities(self):
        if self.history is not None and hasattr(self.history, "unprofitable_enemies"):
            return self.history.unprofitable_enemies()
        return self.local_avoided

    def _route_toward(self, context, dx_px, dy_px, open_steps):
        """First step of the shortest known route ending nearer the target."""
        size = tuple(context["map_size_tiles"])
        origin = tuple(context["actor_tile"])
        cells = self._known_cells(context["map_id"], context["actor"]["layer"])
        cells = dict(cells)
        cells[origin] = "open"
        routes = self._routes(origin, cells, size,
                              context["actor"].get("floor_height"),
                              forbidden=self._forbidden_steps(context))
        goal = (origin[0] + round(dx_px / 16), origin[1] + round(dy_px / 16))
        start_gap = abs(goal[0] - origin[0]) + abs(goal[1] - origin[1])
        best = None
        for position, (first, distance) in routes.items():
            if first is None or first not in open_steps:
                continue
            gap = (abs(self._wrapped_delta(goal[0], position[0], size[0])) +
                   abs(self._wrapped_delta(goal[1], position[1], size[1])))
            if gap >= start_gap:
                continue
            candidate = (gap, distance, first, position)
            if best is None or candidate < best:
                best = candidate
        return (best[2], best[1], routes.straight(best[3])) if best else None

    def _travel(self, context, anchor, open_steps, place, travel_intent, arrive_intent, strict=False):
        """Follow learned maps and tiles to a taught place; None if no route."""
        actor = context["actor"]
        destination = anchor
        exit_action = None
        crossing = False
        if (isinstance(anchor, dict) and anchor.get("map_id") != context.get("map_id") and
                self.history is not None):
            crossing = True
            waypoints = []
            if hasattr(self.history, "exit_waypoints"):
                waypoints = self.history.exit_waypoints(context.get("map_id"),
                                                        anchor.get("map_id"))
                offered_waypoints = bool(waypoints)
                waypoints = self._doors_toward_room(waypoints, anchor)
                if offered_waypoints and not waypoints:
                    return PlannerDecision("wait", "no doorway leads to the destination room", "observe")
            destination = (self._pick_waypoint(context, waypoints) if waypoints else
                           self.history.return_waypoint(context.get("map_id"),
                                                        anchor.get("map_id")))
            exit_action = destination.get("action") if isinstance(destination, dict) else None
            door = self._door_for_crossing(context, destination)
            if door is not None:
                # The recorded tile of a return-door crossing is the last tile
                # verified before the fade, often short of the door; heading
                # for it would bounce. Walk onto the native door instead.
                decision = self._return_door(context, open_steps,
                                             f"leave by return door toward {place}")
                if decision.intention == "leave_room":
                    return replace(decision, intention=travel_intent)
        if (isinstance(destination, dict) and
                destination.get("map_id") == context.get("map_id") and
                destination.get("layer") == actor.get("layer") and
                isinstance(destination.get("target"), list)):
            position = tuple(context["actor_tile"])
            target = tuple(destination["target"])
            cells = self._exit_cells(context, destination)
            weights = None
            if strict or travel_intent == "return_to_inn":
                width, height = context["map_size_tiles"]
                weights = {}
                for pos, kind in cells.items():
                    risk = 0
                    for ex, ey in self.route_threats:
                        dx = abs(self._wrapped_delta(pos[0], ex, width))
                        dy = abs(self._wrapped_delta(pos[1], ey, height))
                        risk += max(0, 10-dx-dy)*8
                    half = pos in self.route_diagonals.get((context["map_id"], actor["layer"]), ())
                    weights[pos] = 1 + risk + (3 if kind == "cuttable" else 0) + (20 if half else 0)
            routes = self._routes(position, cells, tuple(context["map_size_tiles"]),
                                  actor.get("floor_height"), weights,
                                  jumps=self._whip_jumps(context),
                                  forbidden=self._forbidden_steps(context))
            via = destination.get("path")
            hop = (f" via maps {'>'.join(str(m) for m in via)}"
                   if isinstance(via, list) and len(via) > 2 else "")
            if target == position:
                if exit_action in open_steps:
                    return PlannerDecision(exit_action, f"take learned exit toward {place}",
                                           travel_intent, 1)
                if crossing and exit_action is None:
                    # On a ROM exit tile; the game starts the transition.
                    return PlannerDecision("wait", f"on exit to map "
                                           f"{destination.get('next_map')}", travel_intent)
                if exit_action is None:
                    return PlannerDecision("wait", f"at learned {place} point", arrive_intent)
                if (crossing and destination.get("source") == "learned" and
                        hasattr(self.history, "reject_edge")):
                    # Standing on the recorded crossing, but its step is a
                    # verified wall (e.g. a shop counter logged as a map change).
                    # Falling through would bounce between this tile and a
                    # return door forever; drop the edge and route another way.
                    self.history.reject_edge(context.get("map_id"), destination.get("next_map"))
                    return PlannerDecision("wait", f"learned exit to map "
                                           f"{destination.get('next_map')} is blocked "
                                           f"by {exit_action}; rerouting", "replan")
            if target in routes and target != position:
                action, distance = routes[target]
                if action.startswith("whip:"):
                    return self._whip_cross(context, action[5:],
                                            f"learned crossing toward {place}")
                if action in open_steps:
                    kind = ("exit to map %s%s" % (destination.get("next_map"), hop)
                            if crossing else "")
                    corridor = []
                    cursor = target
                    while cursor != position and cursor in routes.parents:
                        corridor.append(cursor)
                        cursor = routes.parents[cursor][0]
                    corridor.reverse()
                    safe_corridor = []
                    for point in corridor[:3]:
                        if cells.get(point) not in ("open","pass","h1","h2"):
                            break
                        if (abs(self._wrapped_delta(point[0],position[0],context["map_size_tiles"][0])) > 4 or
                                abs(self._wrapped_delta(point[1],position[1],context["map_size_tiles"][1])) > 4):
                            break
                        safe_corridor.append(point)
                    return PlannerDecision(action,
                                           f"go to learned {place} ({distance} tiles)"
                                           + (f" by {kind}" if kind else ""),
                                           travel_intent, distance, routes.straight(target),
                                           navigation_path=tuple(safe_corridor))
                if action in self._cut_steps(context):
                    return self._cut(context, action)
            if target not in routes and context.get("return_doors"):
                # A shared map id does not mean the shop is in this room.
                # Leave through a native return door, then choose the entrance
                # whose arrival belongs to the destination's connected room.
                return self._return_door(context, open_steps, f"leave disconnected room for {place}")
            if strict:
                return PlannerDecision("wait", "no verified route to destination exit", "observe")
            # Scanned collision islands can be disconnected when the agent
            # crossed an area before map learning was enabled. Keep a stable
            # heading toward the observed exit while checking every next tile.
            size = tuple(context["map_size_tiles"])
            dx = self._wrapped_delta(target[0], position[0], size[0])
            dy = self._wrapped_delta(target[1], position[1], size[1])
            action = self._approach_direction(dx, dy, open_steps)
            if action:
                return PlannerDecision(action,
                                       f"head toward learned {place} exit at "
                                       f"{target[0]},{target[1]} vector dx={dx},dy={dy}",
                                       travel_intent)
        return None

    def _doors_toward_room(self, waypoints, anchor):
        if not isinstance(anchor.get("target"), list):
            return waypoints
        target_map, layer = anchor.get("map_id"), anchor.get("layer")
        size = self.history.get_agent_state(f"door_map_size_{target_map}")
        cells = self.history.known_cells(target_map, layer)
        target = tuple(anchor["target"])
        if not size or target not in cells:
            return waypoints
        room = self._routes(target, cells, tuple(size))
        matching, unknown = [], []
        for waypoint in waypoints:
            arrival = waypoint.get("arrival")
            if waypoint.get("next_map") != target_map or not isinstance(arrival, list):
                unknown.append(waypoint)
            elif waypoint.get("arrival_layer") == layer and tuple(arrival) in room:
                matching.append(waypoint)
            elif waypoint.get("arrival_layer") == layer and tuple(arrival) not in cells:
                unknown.append(waypoint)
        # Prefer a proven connected arrival over ambiguous old map_edges.
        return matching or unknown

    @staticmethod
    def _door_for_crossing(context, destination):
        """Return door this learned crossing was made through, if any."""
        if not isinstance(destination, dict) or destination.get("source") != "learned":
            return None
        target = destination.get("target")
        if (destination.get("map_id") != context.get("map_id") or
                destination.get("layer") != context["actor"].get("layer") or
                not isinstance(target, list)):
            return None
        near = [door for door in context.get("return_doors") or []
                if abs(door["target"][0] - target[0]) + abs(door["target"][1] - target[1])
                <= DOOR_CROSSING_TILES]
        return near[0] if near else None

    def _return_door(self, context, open_steps, reason="leave room through return door"):
        position = tuple(context["actor_tile"])
        cells = self._known_cells(context["map_id"], context["actor"]["layer"])
        routes = self._routes(position, cells, tuple(context["map_size_tiles"]),
                              context["actor"].get("floor_height"),
                              forbidden=self._forbidden_steps(context))
        choices = []
        for door in context.get("return_doors") or []:
            target = tuple(door["target"])
            if target == position:
                return PlannerDecision("wait", "on native return door; awaiting crossing", "leave_room")
            if target in routes and routes[target][0] in open_steps:
                choices.append((routes[target][1], target))
        if choices:
            distance, target = min(choices)
            return PlannerDecision(routes[target][0], reason, "leave_room", distance,
                                   routes.straight(target))
        return PlannerDecision("wait", "return door has no verified route", "observe")

    def _exit_cells(self, context, destination, cells=None):
        """Known cells; a ROM exit tile is entered whatever its looks say.

        Trigger tiles copy the attributes of the tile they replace, so most
        read as open; the guard still checks the real step into it.
        """
        if cells is None:
            cells = self._known_cells(context["map_id"], context["actor"]["layer"])
        if destination.get("source") == "rom":
            target = tuple(destination["target"])
            if cells.get(target) not in ("open", "pass", "cuttable", "h1", "h2"):
                cells = dict(cells)
                cells[target] = "pass"
        return cells

    def _pick_waypoint(self, context, waypoints):
        """Nearest reachable exit on the hero's layer, else the first one."""
        actor = context["actor"]
        position = tuple(context["actor_tile"])
        size = tuple(context["map_size_tiles"])
        usable = [waypoint for waypoint in waypoints
                  if waypoint.get("layer") == actor.get("layer") and
                  isinstance(waypoint.get("target"), list)]
        # One search for all exits (map 131 has 63 painted border exit tiles).
        cells = dict(self._known_cells(context["map_id"], actor["layer"]))
        for waypoint in usable:
            if waypoint.get("source") == "rom":
                cells.update(self._exit_cells(context, waypoint, cells))
        routes = self._routes(position, cells, size, actor.get("floor_height"),
                              forbidden=self._forbidden_steps(context))
        best = None
        for order, waypoint in enumerate(usable):
            target = tuple(waypoint["target"])
            if target == position:
                return waypoint
            if target in routes:
                # A cross-checked ROM exit beats a learned crossing, whose
                # recorded tile can be off (return doors, scripted moves).
                candidate = (waypoint.get("source") != "rom", routes[target][1], order)
                if best is None or candidate < best[0]:
                    best = (candidate, waypoint)
        if best is not None:
            return best[1]
        on_layer = [w for w in waypoints if w.get("layer") == actor.get("layer")]
        return (on_layer or waypoints)[0]

    def _survive(self, observation, context, open_steps, critical=False):
        """Disengage and retrace the learned map toward the inn/safe anchor."""
        actor = context["actor"]
        dimensions = context.get("map_size_tiles", [0, 0])
        width_px, height_px = dimensions[0] * 16, dimensions[1] * 16
        enemies = [enemy for enemy in observation.get("enemy_slots", [])
                   if enemy.get("active")]
        threat_retreat = None
        if enemies and open_steps:
            # Survival outranks the route: first increase distance from the
            # closest current threat, then continue toward safety.
            scored = []
            for action in open_steps:
                sx, sy = STEP_OFFSETS[action]
                nearest = min(
                    abs(self._wrapped_delta(enemy["x_px"], actor["x_px"] + sx * 16,
                                            width_px)) +
                    abs(self._wrapped_delta(enemy["y_px"], actor["y_px"] + sy * 16,
                                            height_px))
                    for enemy in enemies)
                scored.append((nearest, action))
            best_distance, threat_retreat = max(scored)
            previous_step = self._get_state("last_evade")
            reverse = OPPOSITE_STEP.get(previous_step)
            by_action = {action: nearest for nearest, action in scored}
            if (previous_step in by_action and
                    by_action[previous_step] + EVADE_HYSTERESIS_PX >= best_distance):
                threat_retreat = previous_step
            elif (threat_retreat == reverse and
                  any(action != reverse and nearest + EVADE_HYSTERESIS_PX >= best_distance
                      for nearest, action in scored)):
                threat_retreat = max((nearest, action) for nearest, action in scored
                                     if action != reverse)[1]
            self._set_state("last_evade", threat_retreat)

        anchor = self.history.safe_anchor() if self.history is not None else None
        travel = self._travel(context, anchor, open_steps, "inn",
                              "return_to_inn", "recover_at_inn")
        if travel is not None:
            return travel

        if threat_retreat:
            return PlannerDecision(threat_retreat,
                                   "evade enemies while searching for inn route",
                                   "return_to_inn")

        # No complete learned route yet. Never resume combat: use the least
        # visited verified tile to search for the recorded return trail.
        if open_steps:
            position = tuple(context["actor_tile"])
            size = tuple(context["map_size_tiles"])
            action = min(open_steps, key=lambda step: self._visits(
                context["map_id"], actor["layer"],
                *self._neighbor(position, STEP_OFFSETS[step], size)))
            return PlannerDecision(action, "search verified return trail to inn",
                                   "return_to_inn")
        if critical and observation.get("weapons_enabled") and enemies:
            return PlannerDecision("attack", "critical and boxed in by enemy", "survive")
        return PlannerDecision("wait", "no safe retreat tile", "survive")

    @staticmethod
    def _approach_direction(dx, dy, open_steps):
        axes = (["step_right" if dx > 0 else "step_left",
                 "step_down" if dy > 0 else "step_up"] if abs(dx) >= abs(dy) else
                ["step_down" if dy > 0 else "step_up",
                 "step_right" if dx > 0 else "step_left"])
        for action in axes:
            if action in open_steps and ((dx and action in ("step_left", "step_right")) or
                                         (dy and action in ("step_up", "step_down"))):
                return action
        return None

    def _update_provoked(self, observation, context, hp):
        """Start (or count down) fight-back mode after a hit from a nearby enemy."""
        actor = context.get("actor") or {}
        near = False
        if isinstance(actor.get("x_px"), int) and isinstance(actor.get("y_px"), int):
            near = any(isinstance(e, dict) and e.get("active") and
                       isinstance(e.get("x_px"), int) and isinstance(e.get("y_px"), int) and
                       abs(e["x_px"] - actor["x_px"]) + abs(e["y_px"] - actor["y_px"]) <=
                       PROVOKE_RANGE_PX
                       for e in observation.get("enemy_slots", []))
        self.just_hit = (isinstance(hp, int) and isinstance(self.last_hp, int) and
                         hp < self.last_hp and near)
        if self.just_hit:
            self.provoked = PROVOKED_DECISIONS
        elif self.provoked:
            self.provoked -= 1
        self.last_hp = hp if isinstance(hp, int) else self.last_hp

    def decide(self, observation, previous="none", directive="explore_unknown", errand=None):
        self.previous_action = previous
        """errand: optional {"place": "shop", "anchor": {...}} to travel to."""
        self.blocked_moves = {key: turns - 1 for key, turns in self.blocked_moves.items()
                              if turns > 1}
        self.whip_blocked = {key: turns - 1 for key, turns in self.whip_blocked.items()
                             if turns > 1}
        self.previous_action = previous
        self.block_cooldown = max(0, self.block_cooldown - 1)
        if previous in STEP_OFFSETS:
            self.assumed_facing = previous
        if previous in ("attack", "charge"):
            # When that swing was decided (sent right after), else now.
            self.last_swing = self.swing_decided_at or self.clock()
        self.swing_decided_at = None
        if self.escape_left and errand is None and directive.startswith("explore_"):
            self.escape_left -= 1
            directive = self.escape_directive
        deflected = self._check_deflection(observation, previous)
        decision = self._decide(observation, previous, directive, errand)
        if deflected:
            decision = replace(decision, reason=f"{deflected}; {decision.reason}")
        decision = self._observe_curiosity(observation, decision, directive, errand)
        decision = self._check_stall(observation, decision)
        return self._slide_failsafe_tick(observation, decision)

    BUSY_INTENTIONS = ("engage_enemy", "survive", "recover_at_inn", "return_to_inn",
                       "at_inn", "errand", "arrive")

    def _observe_curiosity(self, observation, decision, directive, errand):
        """Reward new places, penalise lingering (som_curiosity)."""
        context = observation.get("tile_context") if isinstance(observation, dict) else None
        if (not isinstance(context, dict) or not context.get("runtime_verified") or
                context.get("game_mode") != "field" or not context.get("actor_tile")):
            return decision
        if self.curiosity is None:
            self.curiosity = Curiosity(self._get_state("curiosity"))
        map_id, layer = context.get("map_id"), (context.get("actor") or {}).get("layer")
        tile = tuple(context["actor_tile"])
        enemies = any(e.get("active") for e in observation.get("enemy_slots", []) or [])
        busy = (errand is not None or enemies or self.provoked > 0 or
                decision.intention in self.BUSY_INTENTIONS)
        events = self.curiosity.observe(map_id, layer, tile, busy)
        if not events:
            return decision
        notes = []
        for kind, points, note in events:
            notes.append("%s %+d: %s" % (kind, points, note))
            if kind == "linger":
                # Ban frontier targets around the spot and head elsewhere.
                radius = LINGER_RADIUS + 2
                for dx in range(-radius, radius + 1):
                    for dy in range(-radius, radius + 1):
                        cell = (tile[0] + dx, tile[1] + dy)
                        self.frontier_rejected[(map_id, layer, cell)] = (
                            self.frontier_clock + FRONTIER_COOLDOWN)
                self._set_intention(None)
                self.escape_directive = Curiosity.escape_heading(directive)
                self.escape_left = 40
        self._set_state("curiosity", self.curiosity.to_state())
        return replace(decision, reason="%s; curiosity %s (score %d)" % (
            decision.reason, "; ".join(notes), self.curiosity.score))

    def _cuttable(self):
        if self.history is not None:
            return self.history.cuttable_signatures()
        return self.local_cuttable

    def _cut_steps(self, context, reach=1):
        """Steps into a blocking tile that looks like grass already cut once.

        reach=2 also finds grass two tiles away beyond an open tile: the hero
        box reaches into the next row from inside its tile, and a diagonal
        half tile can refuse the step in between (live map 129, tile 38,23).
        """
        return [action for action, _ in self._cut_targets(context, reach)]

    def _grass_blocking_enemy(self, context, dx, dy):
        """Cut adjacent known vegetation on the path toward a target.

        Only a tile on a relevant target-facing axis is eligible; vegetation
        to the side cannot hijack a fight. The cut signature must already be
        learned from a real terrain transition.
        """
        choices = self._cut_targets(context, reach=2)
        if not choices:
            return None
        options = []
        for action, reach in choices:
            ox, oy = STEP_OFFSETS[action]
            ahead = dx * ox + dy * oy
            side = abs(dx * oy - dy * ox)
            # The enemy must lie beyond the grass along that axis.
            if ahead > 16 * reach and ahead >= side:
                options.append((-reach, ahead, -side, action, reach))
        if not options:
            return None
        best = max(options)
        return best[3], best[4]

    def _cut_targets(self, context, reach=1):
        """[(action, distance)] toward cuttable tiles within reach tiles."""
        rows, attrs, radius = (context.get("passability_rows"), context.get("attribute_rows"),
                               context.get("radius"))
        if not (isinstance(rows, list) and isinstance(attrs, list) and isinstance(radius, int)):
            return []
        cuttable, blocked = self._cuttable(), self._blocked_here(context)
        targets = []
        for action, (dx, dy) in STEP_OFFSETS.items():
            for distance in range(1, reach + 1):
                row, column = radius + dy * distance, radius + dx * distance
                if not (0 <= row < len(rows) and 0 <= column < len(rows[row])):
                    break
                cells = attrs[row].split()
                if (rows[row][column] in "#?" and column < len(cells) and
                        cells[column] in cuttable):
                    # Next to the hero a blocked step means the cut failed;
                    # farther away the step was blocked by what lies between.
                    if distance > 1 or action not in blocked:
                        targets.append((action, distance))
                    break
                if rows[row][column] not in ".@":
                    break
        return targets

    def _cut(self, context, action, distance=1):
        """Face the grass with a short tap (the hero cannot enter it), then attack."""
        tile = context.get("actor_tile") or [0, 0]
        dx, dy = STEP_OFFSETS[action]
        key = (context.get("map_id"), tile[0] + dx * distance, tile[1] + dy * distance)
        if self.previous_action != action:
            return PlannerDecision(action, f"face grass at {key[1]},{key[2]} to cut it",
                                   "cut_grass", turn_only=True)
        attempts = self.cut_attempts.get(key, 0) + 1
        self.cut_attempts[key] = attempts
        if attempts > CUT_ATTEMPTS:
            self.blocked_moves[self._stall_key(context, action)] = STALL_BLOCK_DECISIONS
            self.cut_attempts.pop(key, None)
            return PlannerDecision("wait", f"grass at {key[1]},{key[2]} did not cut after "
                                   f"{CUT_ATTEMPTS} attacks; avoiding it", "replan")
        return PlannerDecision("attack", f"cut grass at {key[1]},{key[2]} "
                               f"(attack {attempts}/{CUT_ATTEMPTS})", "cut_grass")

    # ---- whip crossings -------------------------------------------------
    def _learn_whip(self, observation):
        """Learn a whip crossing that just happened (history or local)."""
        if self.history is not None and hasattr(self.history, "learn_whip_crossings"):
            crossing = self.history.learn_whip_crossings(observation, self.clock)
        else:
            crossing = None
            context = observation.get("tile_context") or {}
            if context.get("runtime_verified") and not hero_airborne(observation):
                now = self.clock()
                anchor, self.whip_anchor = self.whip_anchor, (observation, now)
                if anchor is not None and now - anchor[1] <= 5.0:
                    crossing = detect_whip_crossing(anchor[0], observation)
                if crossing is not None:
                    links = self.local_whip.setdefault(
                        (crossing["map_id"], crossing["layer"]), {})
                    links[(tuple(crossing["from"]), crossing["action"])] = crossing
                    self.local_posts.update(crossing["posts"])
        if crossing is not None:
            key = (crossing["map_id"], crossing["layer"], *crossing["from"], crossing["action"])
            self.whip_attempts.pop(key, None)
            self.whip_blocked.pop(key, None)
            print(f"[whip] learned crossing on map {crossing['map_id']}: "
                  f"{tuple(crossing['from'])} {crossing['action']} -> {tuple(crossing['to'])}"
                  + (f", posts {crossing['posts']}" if crossing["posts"] else ""), flush=True)
        return crossing

    def _learned_crossings(self, map_id, layer):
        if self.history is not None and hasattr(self.history, "whip_crossings"):
            return self.history.whip_crossings(map_id, layer)
        return list(self.local_whip.get((map_id, layer), {}).values())

    def _post_signatures(self):
        if self.history is not None and hasattr(self.history, "whip_post_signatures"):
            return self.history.whip_post_signatures()
        return self.local_posts

    def _whip_key(self, context, action):
        tile = context.get("actor_tile") or [None, None]
        return (context.get("map_id"), (context.get("actor") or {}).get("layer"),
                tile[0], tile[1], action)

    def _whip_jumps(self, context):
        """Learned crossings usable for routes: {from: [(step, to, height)]}."""
        if not self.whip_ready:
            return None
        map_id, layer = context.get("map_id"), (context.get("actor") or {}).get("layer")
        jumps = {}
        for crossing in self._learned_crossings(map_id, layer):
            start, action = tuple(crossing["from"]), crossing["action"]
            if (map_id, layer, *start, action) in self.whip_blocked:
                continue
            if crossing.get("to_layer") not in (None, layer):
                continue
            jumps.setdefault(start, []).append(
                (action, tuple(crossing["to"]), crossing.get("to_height")))
        return jumps

    def _whip_options(self, context):
        """{step: reason} for whip swings worth trying from this tile.

        A learned crossing from here; else a recognised post (learned post
        attributes) within reach with floor beyond the wall; else, once per
        run, any wall or ledge with floor two to five tiles beyond it.
        """
        if not self.whip_ready:
            return {}
        tile = tuple(context.get("actor_tile") or ())
        map_id, layer = context.get("map_id"), (context.get("actor") or {}).get("layer")
        options = {}
        for crossing in self._learned_crossings(map_id, layer):
            if tuple(crossing["from"]) == tile:
                to = tuple(crossing["to"])
                options[crossing["action"]] = f"learned crossing to {to[0]},{to[1]}"
        rows, terrain, attrs, radius = (context.get("passability_rows"),
                                        context.get("terrain_rows"),
                                        context.get("attribute_rows"), context.get("radius"))
        if not (isinstance(rows, list) and isinstance(terrain, list) and
                isinstance(attrs, list) and isinstance(radius, int)):
            return {a: w for a, w in options.items()
                    if self._whip_key(context, a) not in self.whip_blocked}
        posts = self._post_signatures()
        for action, (dx, dy) in STEP_OFFSETS.items():
            if action in options:
                continue
            landing, post = self._whip_scan(rows, terrain, attrs, radius, radius,
                                            dx, dy, min(WHIP_REACH_TILES, radius), posts)
            if landing is None:
                continue
            if post:
                options[action] = f"whip post within {landing} tiles"
            elif self._whip_key(context, action) not in self.whip_probed:
                options[action] = f"probe: floor {landing} tiles beyond the ledge"
        return {a: w for a, w in options.items()
                if self._whip_key(context, a) not in self.whip_blocked}

    @staticmethod
    def _whip_scan(rows, terrain, attrs, row0, column0, dx, dy, reach, posts):
        """(landing distance or None, post seen) for a swing from window cell
        (row0, column0): a wall or ledge first, then floor 2+ tiles out."""
        barrier, post = False, False
        for distance in range(1, reach + 1):
            row, column = row0 + dy * distance, column0 + dx * distance
            if not (0 <= row < len(rows) and 0 <= column < len(rows[row])):
                break
            cells = attrs[row].split() if row < len(attrs) else []
            if column < len(cells) and (cells[column] in posts or
                                        is_whip_post(cells[column])):
                post = True
            # Terrain codes are height-independent: the floor below a
            # ledge is "#" in the hero-relative markers but "1"/"2" here.
            line = terrain[row] if row < len(terrain) else ""
            code = line[column] if column < len(line) else "#"
            if barrier and distance >= 2 and code in ".12=":
                return distance, post
            if rows[row][column] in "#?":
                barrier = True
            elif not barrier:
                break     # open ground first: just walk
        return None, post

    def _post_approach(self, context, routes, open_steps):
        """Head straight for a visible whip post instead of waiting to stall
        at the ledge: every open tile in view that has a post in line across
        a gap is a launch spot; on one, swing; else route to the nearest.
        Spots whose landing tile was already stood on are skipped (no
        crossing back and forth), learned crossings are left to routing."""
        rows, terrain, attrs, radius = (context.get("passability_rows"),
                                        context.get("terrain_rows"),
                                        context.get("attribute_rows"), context.get("radius"))
        if not (isinstance(rows, list) and isinstance(terrain, list) and
                isinstance(attrs, list) and isinstance(radius, int)):
            return None
        map_id, layer = context.get("map_id"), (context.get("actor") or {}).get("layer")
        here = tuple(context.get("actor_tile") or ())
        if len(here) != 2:
            return None
        learned = {(tuple(c["from"]), c["action"]) for c in self._learned_crossings(map_id, layer)}
        posts = self._post_signatures()
        best = None
        for row in range(len(rows)):
            for column in range(len(rows[row])):
                if rows[row][column] in "#?":
                    continue
                tile = (here[0] + column - radius, here[1] + row - radius)
                distance = 0 if tile == here else (routes.get(tile) or (None, None))[1]
                if tile != here and (distance is None or tile not in routes):
                    continue
                for action, (dx, dy) in STEP_OFFSETS.items():
                    if (tile, action) in learned or (map_id, layer, *tile, action) in self.whip_blocked:
                        continue
                    reach = min(WHIP_REACH_TILES, radius)
                    landing, post = self._whip_scan(rows, terrain, attrs, row, column,
                                                    dx, dy, reach, posts)
                    if landing is None or not post:
                        continue
                    far = (tile[0] + dx * landing, tile[1] + dy * landing)
                    if self._visits(map_id, layer, *far):
                        continue
                    if best is None or distance < best[0]:
                        best = (distance, tile, action, landing)
        if best is None:
            return None
        distance, tile, action, landing = best
        why = f"whip post within {landing} tiles"
        if tile == here:
            return self._whip_cross(context, action, why)
        step = routes[tile][0]
        if step not in open_steps:
            return None
        return PlannerDecision(step, f"to the whip post launch tile {tile[0]},{tile[1]} "
                               f"({distance} tiles), then whip {action[5:]}",
                               "whip_cross", distance, routes.straight(tile))

    def _whip_cross(self, context, action, why):
        """Face across the gap with a tap, then swing the whip (the bridge
        equips it first for intention whip_cross)."""
        key = self._whip_key(context, action)
        if self.previous_action != action:
            return PlannerDecision(action, f"face {action[5:]} to swing the whip ({why})",
                                   "whip_cross", turn_only=True)
        probe = why.startswith("probe")
        limit = WHIP_PROBE_ATTEMPTS if probe else WHIP_ATTEMPTS
        attempts = self.whip_attempts.get(key, 0) + 1
        self.whip_attempts[key] = attempts
        if probe:
            self.whip_probed.add(key)
        if attempts > limit and not probe:
            moved = self.whip_repositions.get(key, 0)
            nudge = self._whip_nudge(context, action, moved)
            if moved < WHIP_REPOSITIONS and nudge is not None:
                self.whip_repositions[key] = moved + 1
                self.whip_attempts.pop(key, None)
                return PlannerDecision(nudge, f"whip {action[5:]} missed {limit} times; "
                                       f"shifting {nudge[5:]} to try again "
                                       f"({moved + 1}/{WHIP_REPOSITIONS})", "whip_cross")
        if attempts > limit:
            self.whip_attempts.pop(key, None)
            self.whip_repositions.pop(key, None)
            self.whip_blocked[key] = WHIP_BLOCK_DECISIONS
            self._set_intention(None)
            return PlannerDecision("wait", f"whip {action[5:]} from {key[2]},{key[3]} did not "
                                   f"cross after {limit} swing(s); trying other ways", "replan")
        return PlannerDecision("attack", f"whip across: {why} (swing {attempts}/{limit})",
                               "whip_cross")

    @staticmethod
    def _whip_nudge(context, action, moved):
        """A short step that changes the stance for another whip try: first
        across the swing line toward the other half of the tile, then back
        from the edge (a different distance to the post)."""
        actor = context.get("actor") or {}
        if action in ("step_left", "step_right"):
            within = actor.get("y_px")
            across = ("step_down", "step_up")
        else:
            within = actor.get("x_px")
            across = ("step_right", "step_left")
        if not isinstance(within, int):
            return None
        toward_other_half = across[0] if within % 16 < 8 else across[1]
        return (toward_other_half, OPPOSITE_STEP[action])[min(moved, 1)]

    @staticmethod
    def _stall_key(context, action):
        tile = context.get("actor_tile") or [None, None]
        actor = context.get("actor") or {}
        return f"{context.get('map_id')}:{actor.get('layer')}:{tile[0]}:{tile[1]}:{action}"

    def _dead_end_exits(self, map_id, layer):
        """Exit tiles of this map into maps that only lead back here.

        Exploring treats unknown exits as frontiers, so a fully known side
        room whose every exit returns here pulled the hero through its door
        and straight back out again: maps 136/109 57 times (16 Nov). Once
        the hero has entered such a map DEAD_END_VISITS times its door is a
        wall while exploring (travel and errands still route through it).
        """
        if not hasattr(self.history, "exits_by_destination"):
            return set()
        walls = set()
        for to_map, tiles in self.history.exits_by_destination(map_id).items():
            if to_map == map_id:
                continue
            onward = set(self.history.exits_by_destination(to_map))
            if (onward and onward <= {map_id} and
                    self.history.crossings_into(to_map) >= DEAD_END_VISITS):
                walls.update((x, y) for tile_layer, x, y in tiles
                             if tile_layer in (None, layer))
        return walls

    def _forbidden_steps(self, context):
        """{(tile, step)} blocked on this map and layer, for route searches."""
        actor = context.get("actor") or {}
        prefix = f"{context.get('map_id')}:{actor.get('layer')}:"
        forbidden = set()
        remembered = () if self.slide_failsafe else self._slides()
        for key in list(self.blocked_moves) + list(remembered):
            if key.startswith(prefix):
                x, y, action = key[len(prefix):].split(":")
                if x.lstrip("-").isdigit() and y.lstrip("-").isdigit():
                    forbidden.add(((int(x), int(y)), action))
        return forbidden or None

    def _check_deflection(self, observation, previous):
        """Block a step that keeps landing the hero beside its target tile."""
        context = observation.get("tile_context") if isinstance(observation, dict) else None
        origin, self.last_step_origin = self.last_step_origin, None
        if (not isinstance(context, dict) or not context.get("runtime_verified") or
                context.get("game_mode") != "field"):
            return None
        actor = context.get("actor") or {}
        tile, size = context.get("actor_tile"), context.get("map_size_tiles")
        if not (isinstance(tile, list) and len(tile) == 2 and
                isinstance(size, list) and len(size) == 2):
            return None
        here = (context.get("map_id"), actor.get("layer"), tuple(tile))
        self.last_step_origin = here
        if (origin is None or previous not in STEP_OFFSETS or self.just_hit or
                origin[:2] != here[:2] or origin[2] == here[2]):
            return None
        start = origin[2]
        key = (origin[0], origin[1], start[0], start[1], previous)
        expected = self._neighbor(start, STEP_OFFSETS[previous], tuple(size))
        stall_key = f"{origin[0]}:{origin[1]}:{start[0]}:{start[1]}:{previous}"
        if here[2] == expected:
            self.deflections.pop(key, None)
            if stall_key in self._slides():
                self._remember_slide(stall_key, False)
                return f"{previous} from tile {list(start)} works after all; forgetting that slide"
            return None
        dx = self._wrapped_delta(here[2][0], start[0], size[0])
        dy = self._wrapped_delta(here[2][1], start[1], size[1])
        if max(abs(dx), abs(dy)) > 1:
            return None   # knockback or a warp, not a slide
        self.deflections[key] = self.deflections.get(key, 0) + 1
        if self.deflections[key] < DEFLECT_LIMIT:
            return None
        self.deflections.pop(key, None)
        self.blocked_moves[stall_key] = STALL_BLOCK_DECISIONS
        self._remember_slide(stall_key, True)
        self._set_intention(None)
        return (f"{previous} from tile {list(start)} slid to {list(here[2])} "
                f"instead of {list(expected)} {DEFLECT_LIMIT} times; remembering "
                f"that slide")

    def _slides(self):
        """Remembered slide steps {"map:layer:x:y:step"} (persistent)."""
        if self.slides is None:
            stored = self._get_state("slides")
            self.slides = set(stored) if isinstance(stored, list) else set()
        return self.slides

    def _remember_slide(self, key, slides):
        remembered = self._slides()
        if slides == (key in remembered):
            return
        remembered.add(key) if slides else remembered.discard(key)
        self._set_state("slides", sorted(remembered))

    def _slide_failsafe_tick(self, observation, decision):
        """Count field waits; after SLIDE_FAILSAFE_WAITS in a row stop
        trusting remembered slides for a while."""
        context = observation.get("tile_context") if isinstance(observation, dict) else None
        self.slide_failsafe = max(0, self.slide_failsafe - 1)
        idle = (decision.action == "wait" and decision.intention != "observe" and
                isinstance(context, dict) and context.get("runtime_verified") and
                context.get("game_mode") == "field")
        self.field_waits = self.field_waits + 1 if idle else 0
        if self.field_waits < SLIDE_FAILSAFE_WAITS or not self._slides():
            return decision
        self.field_waits = 0
        self.slide_failsafe = SLIDE_FAILSAFE_DECISIONS
        return replace(decision, reason=f"{decision.reason}; failsafe: retrying "
                       f"remembered slides for {SLIDE_FAILSAFE_DECISIONS} decisions")

    def _blocked_here(self, context):
        return {action for action in STEP_OFFSETS
                if self._stall_key(context, action) in self.blocked_moves}

    def _check_stall(self, observation, decision):
        context = observation.get("tile_context") if isinstance(observation, dict) else None
        if (not isinstance(context, dict) or decision.action not in STEP_OFFSETS or
                decision.turn_only):
            return decision
        actor = context.get("actor") or {}
        position = (context.get("map_id"), actor.get("x_px"), actor.get("y_px"))
        if (self.stall and self.stall["position"] == position and
                self.stall["action"] == decision.action):
            self.stall["count"] += 1
        else:
            self.stall = {"position": position, "action": decision.action, "count": 1}
        if self.stall["count"] < STALL_DECISIONS:
            return decision
        self.blocked_moves[self._stall_key(context, decision.action)] = STALL_BLOCK_DECISIONS
        self.stall = None
        self._set_intention(None)
        # Grass just beyond a refused step: cut it instead of giving up.
        for action, distance in self._cut_targets(context, reach=2):
            if action == decision.action:
                return self._cut(context, action, distance)
        # A ledge or gap with floor beyond it: try the whip (posts).
        why = self._whip_options(context).get(decision.action)
        if why is not None:
            return self._whip_cross(context, decision.action, why)
        return PlannerDecision(
            "wait", f"no progress with {decision.action} from tile "
            f"{context.get('actor_tile')}; blocking that step for "
            f"{STALL_BLOCK_DECISIONS} decisions", "replan")

    def _decide(self, observation, previous, directive, errand):
        context = observation.get("tile_context") if isinstance(observation, dict) else None
        if isinstance(context, dict) and not context.get("runtime_verified"):
            taught = self._follow_taught_path(context, previous, directive)
            if taught is not None:
                return taught
        if not isinstance(context, dict) or not context.get("runtime_verified"):
            why = context.get("verification_error") if isinstance(context, dict) else None
            mode = context.get("game_mode") if isinstance(context, dict) else None
            detail = f" ({why}, mode={mode})" if why else ""
            return PlannerDecision("wait", f"no runtime-verified tile context{detail}",
                                   "observe")
        if context.get("game_mode") != "field":
            return PlannerDecision("wait", f"game mode is {context.get('game_mode')}", "observe")
        status = ((observation.get("party") or [{}])[0] or {}).get("status") or {}
        if status.get("verified") and status.get("controls_locked"):
            # The game ignores the pad (frozen, petrified, balloon, engulfed);
            # stepping would only be mistaken for a wall by stall detection.
            self.stall = None
            return PlannerDecision("wait", "controls locked by status "
                                   f"{', '.join(status.get('effects') or ['?'])}", "observe")
        open_steps = self._passable_steps(context)
        self._learn_facing(context)
        self.whip_ready = ("whip" in (observation.get("owned_weapons") or ()) or
                           held_weapon(observation) == "whip")
        self._remember(observation, context)
        hero = (observation.get("party") or [{}])[0]
        hp, max_hp = hero.get("hp", 0), hero.get("max_hp", 0)
        self._update_provoked(observation, context, hp)
        survival = self._get_state("survival")
        if not isinstance(survival, dict):
            survival = {}
        if self.no_retreat and survival.get("active"):
            survival = {}
            self._set_state("survival", None)
        critical = bool(max_hp and hp * 4 <= max_hp)
        if max_hp and hp * 10 >= max_hp * 9:
            # Real recovery ends survival and any earlier give-up.
            if survival:
                self._set_state("survival", None)
            survival = {}
        elif (max_hp and hp <= max_hp * self.retreat_fraction and not survival.get("active")
              and not self.no_retreat):
            # After a give-up, only a new critical drop restarts the retreat.
            if not survival.get("abandoned") or critical:
                survival = {"active": True, "turns": 0, "at_inn": 0}
                self._set_state("survival", survival)
        if survival.get("active") and (not self.provoked or critical):
            # Hysteresis prevents bouncing between fighting and retreating.
            decision = self._survive(observation, context, open_steps, critical=critical)
            survival["turns"] = survival.get("turns", 0) + 1
            if decision.intention == "recover_at_inn":
                survival["at_inn"] = survival.get("at_inn", 0) + 1
            reason = None
            if survival["at_inn"] >= INN_WAIT_DECISIONS:
                reason = ("reached the inn spot but HP did not rise; staying at the inn "
                          "needs the innkeeper dialog, which is not automated")
            elif survival["turns"] >= SURVIVAL_GIVE_UP_DECISIONS:
                reason = f"no recovery after {SURVIVAL_GIVE_UP_DECISIONS} decisions"
            if reason:
                self._set_state("survival", {"active": False, "abandoned": True})
                self._set_intention(None)
                return PlannerDecision("wait", f"survival abandoned: {reason}; "
                                       "resuming cautious play", "replan")
            self._set_state("survival", survival)
            return decision
        if (not self.provoked and isinstance(errand, dict) and errand.get("avoid_fights") and
                isinstance(errand.get("anchor"), dict) and max_hp and hp*2 > max_hp):
            place = errand.get("place", "destination")
            travel = self._travel(context, errand["anchor"], open_steps, place,
                                  "go_to_destination", "at_destination", strict=True)
            if travel is not None:
                return travel
        combat = self._combat(observation, context, open_steps, directive)
        if combat is not None:
            return combat
        if isinstance(errand, dict) and isinstance(errand.get("anchor"), dict):
            place = errand.get("place", "errand")
            slug = str(place).replace(" ", "_")
            travel = self._travel(context, errand["anchor"], open_steps, place,
                                  f"go_to_{slug}", errand.get("arrive", f"at_{slug}"))
            if travel is not None:
                return travel
        if context.get("return_doors"):
            # Rooms sharing a map id carry each other's door tiles (map 258,
            # 2026-11-14: every door was in another room, the ROM exit 10
            # steps away). An unreachable door must not freeze the hero:
            # fall through to exploring instead of waiting on it forever.
            decision = self._return_door(context, open_steps)
            if decision.intention != "observe":
                return decision
        if not open_steps:
            targets = dict(self._cut_targets(context))
            if not targets:
                targets = dict(self._cut_targets(context, reach=2))
            if targets:
                heading = {"explore_west": "step_left", "explore_east": "step_right",
                           "explore_north": "step_up",
                           "explore_south": "step_down"}.get(directive)
                action = heading if heading in targets else next(iter(targets))
                return self._cut(context, action, targets[action])
            self._set_intention(None)
            return PlannerDecision("wait", "no verified open adjacent tile", "stuck")
        exploration = self._explore(context, open_steps, directive)
        if exploration is not None:
            return exploration
        reverse = OPPOSITE_STEP.get(previous)
        candidates = [step for step in open_steps if step != reverse] or open_steps
        action = min(candidates, key=lambda step: self._visits(
            context["map_id"], context["actor"]["layer"],
            *self._neighbor(tuple(context["actor_tile"]), STEP_OFFSETS[step],
                            tuple(context["map_size_tiles"]))))
        return PlannerDecision(action, "replan after blocked or completed intention", "replan")

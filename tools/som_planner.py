"""Persistent, intention-driven, fail-closed field planner for Secret of Mana."""
import math
import heapq
import itertools
from collections import deque
from dataclasses import dataclass

from som_memory import detect_cuts

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
DEFAULT_STRIKE_AXIAL_PX = 28
DEFAULT_STRIKE_SIDE_PX = 14
# Decisions to ignore an enemy whose straight line and known routes are walled.
UNREACHABLE_COOLDOWN = 20
# Leave survival when the inn spot does not heal or no recovery happens.
# HP does not regenerate on its own, so an endless retreat is a dead end.
INN_WAIT_DECISIONS = 30
SURVIVAL_GIVE_UP_DECISIONS = 900
# Attacks on one grass tile before the planner stops trying to cut it.
CUT_ATTEMPTS = 3
# A step chosen this many times without the hero moving is blocked for a while.
STALL_DECISIONS = 8
STALL_BLOCK_DECISIONS = 200
# Keep the current evasion heading unless another step is clearly safer.
EVADE_HYSTERESIS_PX = 16
# Facing byte E010 is trusted after this many steps agreed with it.
FACING_MIN_CONFIRMED = 3
# An enemy this far sideways still counts as in front when it is no farther
# sideways than ahead.
FRONT_SIDE_PX = 20
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
        self.combat_learner = None  # Optional, trained only from verified live attacks
        self.last_pixel = None
        self.block_cooldown = 0
        self.route_diagonals = {}
        self.route_threats = []

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
    def _routes(start, cells, size, height=None, weights=None):
        """Shortest known paths as {position: (first action, distance)}.

        Searches (position, height) states so a bridge reached up a ramp is
        usable even though, seen from the river bank, its ends look like walls.
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
                for action, offset in STEP_OFFSETS.items():
                    neighbor = MemoryPlanner._neighbor(position, offset, size)
                    kind = cells.get(neighbor)
                    if kind not in ("open", "pass", "cuttable", "h1", "h2"):
                        continue
                    after = MemoryPlanner._enter(kind, level)
                    if after is None and kind in ("h1", "h2"):
                        continue
                    total = cost + max(1, weights.get(neighbor, 1))
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
            for action, offset in STEP_OFFSETS.items():
                neighbor = MemoryPlanner._neighbor(position, offset, size)
                kind = cells.get(neighbor)
                if kind not in ("open", "pass", "cuttable", "h1", "h2"):
                    continue
                after = MemoryPlanner._enter(kind, level)
                if after is None and kind in ("h1", "h2"):
                    continue
                if (neighbor, after) in seen:
                    continue
                seen.add((neighbor, after))
                step = action if first is None else first
                if neighbor not in routes:
                    routes[neighbor] = (step, distance + 1)
                    routes.parents[neighbor] = (position, action)
                queue.append((neighbor, after, step, distance + 1))
        return routes

    def _choose_frontier(self, context, cells, routes, directive="explore_unknown"):
        map_id, layer = context["map_id"], context["actor"]["layer"]
        size = tuple(context["map_size_tiles"])
        origin = tuple(context["actor_tile"])
        direction = directive.removeprefix("explore_")
        candidates = []
        for position, (first, distance) in routes.items():
            if first is None:
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
            candidates.append((not aligned, visits, -progress, straightness,
                               -distance, -unknown_edges, position))
        if not candidates:
            for position, (_, distance) in routes.items():
                if distance:
                    candidates.append((False, self._visits(map_id, layer, *position),
                                       -distance, 0, -distance, 0, position))
        return min(candidates)[-1] if candidates else None

    def _explore(self, context, open_steps, directive="explore_unknown"):
        map_id, layer = context["map_id"], context["actor"]["layer"]
        position = tuple(context["actor_tile"])
        size = tuple(context["map_size_tiles"])
        cells = self._known_cells(map_id, layer)
        routes = self._routes(position, cells, size,
                              context["actor"].get("floor_height"))
        intention = self._get_intention()
        valid = (isinstance(intention, dict) and intention.get("kind") == "explore" and
                 intention.get("map_id") == map_id and intention.get("layer") == layer and
                 intention.get("directive") == directive and
                 isinstance(intention.get("target"), list))
        target = tuple(intention["target"]) if valid else None
        if target == position or target not in routes:
            target = None
        if target is None:
            target = self._choose_frontier(context, cells, routes, directive)
            if target is None:
                self._set_intention(None)
                return None
            intention = {"kind": "explore", "map_id": map_id, "layer": layer,
                         "directive": directive,
                         "target": list(target), "last_position": list(position),
                         "stagnant": 0}
        else:
            last = tuple(intention.get("last_position", []))
            intention["stagnant"] = intention.get("stagnant", 0) + 1 if last == position else 0
            intention["last_position"] = list(position)
            if intention["stagnant"] >= 8:
                self._set_intention(None)
                return None
        action, distance = routes[target]
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
        cooldowns = self._get_state("unreachable") or {}
        if not isinstance(cooldowns, dict):
            cooldowns = {}
        cooldowns = {slot: turns - 1 for slot, turns in cooldowns.items() if turns > 1}
        self._set_state("unreachable", cooldowns or None)
        enemies = [enemy for enemy in enemies if str(enemy.get("slot")) not in cooldowns]
        # Reward ledger: types that cost far more HP than the GP they pay
        # are not engaged, and the hero backs away when one comes close.
        avoided = set() if self.provoked else self._avoided_identities()
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
        hero = observation.get("party", [{}])[0]
        split = self._unsplit([value[3] for value in targets], actor,
                              (width_px, height_px), open_steps)
        if split is not None and not (hero.get("max_hp") and
                                      hero.get("hp", 0) * 4 <= hero["max_hp"]):
            return split
        hp, max_hp = hero.get("hp", 0), hero.get("max_hp", 0)
        if not self.provoked and ((max_hp and hp * 4 <= max_hp) or directive == "avoid_enemy"):
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
        # Turbo's R guard blocks physical hits. Defend briefly when the
        # weapon is still recharging and an enemy is already in striking
        # distance. Cooldown prevents standing still blocking forever.
        if self.block_learner is not None:
            wants_block = (recharge is not None and recharge > 0 and distance <= 40 and
                           self.block_learner.should_block(enemy.get("identity")))
        else:
            wants_block = (recharge is not None and recharge >= 50 and distance <= 32 and
                           max_hp and hp * 4 <= max_hp * 3)
        if wants_block and self.block_cooldown == 0:
            self.block_cooldown = 4 if self.block_learner is not None else 10
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
                return self._cut(context, cutting)

        # Actual melee reach is much shorter than the old 48x48 test.
        # Require a forward axial distance and narrow lateral alignment.
        # The range learner may refine approach choices after valid hits.
        attack_aligned = ((0 < abs(dx) <= DEFAULT_STRIKE_AXIAL_PX and
                           abs(dy) <= DEFAULT_STRIKE_SIDE_PX) or
                          (0 < abs(dy) <= DEFAULT_STRIKE_AXIAL_PX and
                           abs(dx) <= DEFAULT_STRIKE_SIDE_PX))
        if attack_aligned and observation.get("weapons_enabled"):
            # Only a mature learned range can veto a normal swing. Existing
            # attack/charge logic remains the safe fallback for sparse data.
            weapon = hero.get("weapon") or {}
            learner = self.combat_learner
            alone = (len([e for e in observation.get("enemy_slots", [])
                          if e.get("active")]) == 1 and
                     not any(p.get("availability_byte") == 1
                             for p in (observation.get("party") or [])[1:]))
            close_in = (learner is not None and alone and weapon.get("verified") and
                        recharge == 0 and not self.charge_stage(hero, enemy) and
                        learner.should_close(
                            enemy.get("identity"), weapon.get("type"), distance,
                            hp / max_hp if max_hp else 1.0))
            if not close_in:
                strike = self._face_and_strike(context, enemy, dx, dy, open_steps,
                                               recharge == 0, hero)
                if strike is not None:
                    return strike
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

    def _face_and_strike(self, context, enemy, dx, dy, open_steps, ready, hero=None):
        """Attack only while facing the enemy; otherwise tap toward it first.

        With an unverified facing byte the hero always taps toward the enemy
        on the decision before a swing (like facing grass before cutting).
        """
        want = self._toward(dx, dy)
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
                              context["actor"].get("floor_height"))
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
                                  actor.get("floor_height"), weights)
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
            if target in routes and target != position:
                action, distance = routes[target]
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

    def _return_door(self, context, open_steps, reason="leave room through return door"):
        position = tuple(context["actor_tile"])
        cells = self._known_cells(context["map_id"], context["actor"]["layer"])
        routes = self._routes(position, cells, tuple(context["map_size_tiles"]),
                              context["actor"].get("floor_height"))
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
        routes = self._routes(position, cells, size, actor.get("floor_height"))
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
        if isinstance(hp, int) and isinstance(self.last_hp, int) and hp < self.last_hp and near:
            self.provoked = PROVOKED_DECISIONS
        elif self.provoked:
            self.provoked -= 1
        self.last_hp = hp if isinstance(hp, int) else self.last_hp

    def decide(self, observation, previous="none", directive="explore_unknown", errand=None):
        """errand: optional {"place": "shop", "anchor": {...}} to travel to."""
        self.blocked_moves = {key: turns - 1 for key, turns in self.blocked_moves.items()
                              if turns > 1}
        self.previous_action = previous
        self.block_cooldown = max(0, self.block_cooldown - 1)
        if previous in STEP_OFFSETS:
            self.assumed_facing = previous
        decision = self._decide(observation, previous, directive, errand)
        return self._check_stall(observation, decision)

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
        choices = self._cut_targets(context, reach=1)
        if not choices:
            return None
        options = []
        for action, _ in choices:
            ox, oy = STEP_OFFSETS[action]
            ahead = dx * ox + dy * oy
            side = abs(dx * oy - dy * ox)
            if ahead > 0 and ahead >= side:
                options.append((ahead, -side, action))
        return max(options)[2] if options else None

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

    @staticmethod
    def _stall_key(context, action):
        tile = context.get("actor_tile") or [None, None]
        actor = context.get("actor") or {}
        return f"{context.get('map_id')}:{actor.get('layer')}:{tile[0]}:{tile[1]}:{action}"

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
        elif max_hp and hp * 2 <= max_hp and not survival.get("active") and not self.no_retreat:
            # After a give-up, only a new critical drop restarts the retreat.
            if not survival.get("abandoned") or critical:
                survival = {"active": True, "turns": 0, "at_inn": 0}
                self._set_state("survival", survival)
        if survival.get("active") and not self.provoked:
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
            return self._return_door(context, open_steps)
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

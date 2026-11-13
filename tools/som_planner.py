"""Persistent, intention-driven, fail-closed field planner for Secret of Mana."""
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


@dataclass(frozen=True)
class PlannerDecision:
    action: str
    reason: str
    intention: str = "observe"
    remaining_tiles: int | None = None


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
        proven = (self.history.passable_signatures() if self.history is not None
                  else self.local_signatures)
        special = {tuple(cell["offset"]): cell
                   for cell in context.get("special_cells") or []
                   if isinstance(cell.get("offset"), list)}
        for action, offset in STEP_OFFSETS.items():
            if action in steps:
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
        if not (context.get("game_mode") == "field" and
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
            return self.history.known_cells(map_id, layer)
        return {(x, y): kind for (known_map, known_layer, x, y), kind
                in self.local_cells.items() if known_map == map_id and known_layer == layer}

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
    def _routes(start, cells, size, height=None):
        """Shortest known paths as {position: (first action, distance)}.

        Searches (position, height) states so a bridge reached up a ramp is
        usable even though, seen from the river bank, its ends look like walls.
        """
        routes = {start: (None, 0)}
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
                               "explore_frontier", distance)

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
        hp, max_hp = hero.get("hp", 0), hero.get("max_hp", 0)
        if (max_hp and hp * 4 <= max_hp) or directive == "avoid_enemy":
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
        if (abs(dx) <= 48 and abs(dy) <= 48 and
                observation.get("weapons_enabled") and recharge == 0):
            return PlannerDecision("attack", f"enemy {enemy['slot']} in weapon range",
                                   "engage_enemy")
        # Follow the learned map first. Taking the straight-line step whenever
        # its first tile was open, and the map route only when it was not,
        # sent the hero up into a dead end and back down, over and over.
        routed = self._route_toward(context, dx, dy, open_steps)
        if routed:
            action, tiles = routed
            return PlannerDecision(action, f"close on enemy {enemy['slot']} at {distance}px "
                                   f"({tiles} known tiles)", "engage_enemy", tiles)
        action = self._approach_direction(dx, dy, open_steps)
        if action and distance <= CLOSE_QUARTERS_PX:
            return PlannerDecision(action, f"close on enemy {enemy['slot']} at {distance}px",
                                   "engage_enemy")
        cooldowns[str(enemy["slot"])] = UNREACHABLE_COOLDOWN
        self._set_state("unreachable", cooldowns)
        self._set_intention(None)
        return PlannerDecision("wait", f"enemy {enemy['slot']} unreachable; ignoring it for "
                               f"{UNREACHABLE_COOLDOWN} decisions", "engage_enemy")

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
            candidate = (gap, distance, first)
            if best is None or candidate < best:
                best = candidate
        return (best[2], best[1]) if best else None

    def _travel(self, context, anchor, open_steps, place, travel_intent, arrive_intent):
        """Follow learned maps and tiles to a taught place; None if no route."""
        actor = context["actor"]
        destination = anchor
        exit_action = None
        if (isinstance(anchor, dict) and anchor.get("map_id") != context.get("map_id") and
                self.history is not None):
            destination = self.history.return_waypoint(context.get("map_id"),
                                                       anchor.get("map_id"))
            exit_action = destination.get("action") if isinstance(destination, dict) else None
        if (isinstance(destination, dict) and
                destination.get("map_id") == context.get("map_id") and
                destination.get("layer") == actor.get("layer") and
                isinstance(destination.get("target"), list)):
            position = tuple(context["actor_tile"])
            target = tuple(destination["target"])
            cells = self._known_cells(context["map_id"], actor["layer"])
            routes = self._routes(position, cells, tuple(context["map_size_tiles"]),
                                  actor.get("floor_height"))
            if target == position:
                if exit_action in open_steps:
                    return PlannerDecision(exit_action, f"take learned exit toward {place}",
                                           travel_intent, 1)
                if exit_action is None:
                    return PlannerDecision("wait", f"at learned {place} point", arrive_intent)
            if target in routes and target != position:
                action, distance = routes[target]
                if action in open_steps:
                    return PlannerDecision(action,
                                           f"go to learned {place} ({distance} tiles)",
                                           travel_intent, distance)
                if action in self._cut_steps(context):
                    return self._cut(context, action)
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

    def decide(self, observation, previous="none", directive="explore_unknown", errand=None):
        """errand: optional {"place": "shop", "anchor": {...}} to travel to."""
        self.blocked_moves = {key: turns - 1 for key, turns in self.blocked_moves.items()
                              if turns > 1}
        self.previous_action = previous
        decision = self._decide(observation, previous, directive, errand)
        return self._check_stall(observation, decision)

    def _cuttable(self):
        if self.history is not None:
            return self.history.cuttable_signatures()
        return self.local_cuttable

    def _cut_steps(self, context):
        """Steps into a blocking tile that looks like grass already cut once."""
        rows, attrs, radius = (context.get("passability_rows"), context.get("attribute_rows"),
                               context.get("radius"))
        if not (isinstance(rows, list) and isinstance(attrs, list) and isinstance(radius, int)):
            return []
        cuttable, blocked = self._cuttable(), self._blocked_here(context)
        steps = []
        for action, (dx, dy) in STEP_OFFSETS.items():
            row, column = radius + dy, radius + dx
            if not (0 <= row < len(rows) and 0 <= column < len(rows[row])):
                continue
            cells = attrs[row].split()
            if (rows[row][column] in "#?" and column < len(cells) and
                    cells[column] in cuttable and action not in blocked):
                steps.append(action)
        return steps

    def _cut(self, context, action):
        """Face the grass with a short tap (the hero cannot enter it), then attack."""
        tile = context.get("actor_tile") or [0, 0]
        dx, dy = STEP_OFFSETS[action]
        key = (context.get("map_id"), tile[0] + dx, tile[1] + dy)
        if self.previous_action != action:
            return PlannerDecision(action, f"face grass at {key[1]},{key[2]} to cut it",
                                   "cut_grass")
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
        if not isinstance(context, dict) or decision.action not in STEP_OFFSETS:
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
        open_steps = self._passable_steps(context)
        self._remember(observation, context)
        hero = (observation.get("party") or [{}])[0]
        hp, max_hp = hero.get("hp", 0), hero.get("max_hp", 0)
        survival = self._get_state("survival")
        if not isinstance(survival, dict):
            survival = {}
        critical = bool(max_hp and hp * 4 <= max_hp)
        if max_hp and hp * 10 >= max_hp * 9:
            # Real recovery ends survival and any earlier give-up.
            if survival:
                self._set_state("survival", None)
            survival = {}
        elif max_hp and hp * 2 <= max_hp and not survival.get("active"):
            # After a give-up, only a new critical drop restarts the retreat.
            if not survival.get("abandoned") or critical:
                survival = {"active": True, "turns": 0, "at_inn": 0}
                self._set_state("survival", survival)
        if survival.get("active"):
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
        combat = self._combat(observation, context, open_steps, directive)
        if combat is not None:
            return combat
        if isinstance(errand, dict) and isinstance(errand.get("anchor"), dict):
            place = errand.get("place", "errand")
            travel = self._travel(context, errand["anchor"], open_steps, place,
                                  f"go_to_{place}", f"at_{place}")
            if travel is not None:
                return travel
        if not open_steps:
            cut = self._cut_steps(context)
            if cut:
                heading = {"explore_west": "step_left", "explore_east": "step_right",
                           "explore_north": "step_up",
                           "explore_south": "step_down"}.get(directive)
                return self._cut(context, heading if heading in cut else cut[0])
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

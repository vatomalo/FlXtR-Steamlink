"""Persistent, intention-driven, fail-closed field planner for Secret of Mana."""
from collections import deque
from dataclasses import dataclass

STEP_OFFSETS = {
    "step_right": (1, 0), "step_left": (-1, 0),
    "step_up": (0, -1), "step_down": (0, 1),
}
OPPOSITE_STEP = {
    "step_right": "step_left", "step_left": "step_right",
    "step_up": "step_down", "step_down": "step_up",
}


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

    def _remember(self, observation, context):
        if self.history is not None:
            self.history.remember_position(observation)
            self.history.remember_world(observation)
            self.history.remember_recovery(observation)
            self.history.safe_anchor()
            return
        actor = context["actor"]
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
    def _routes(start, cells, size):
        """Return shortest known-open paths as (first action, distance)."""
        routes = {start: (None, 0)}
        queue = deque([start])
        while queue:
            position = queue.popleft()
            first, distance = routes[position]
            for action, offset in STEP_OFFSETS.items():
                neighbor = MemoryPlanner._neighbor(position, offset, size)
                if neighbor in routes or cells.get(neighbor) != "open":
                    continue
                routes[neighbor] = (action if first is None else first, distance + 1)
                queue.append(neighbor)
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
        routes = self._routes(position, cells, size)
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
        targets = []
        for enemy in enemies:
            dx = self._wrapped_delta(enemy["x_px"], actor["x_px"], width_px)
            dy = self._wrapped_delta(enemy["y_px"], actor["y_px"], height_px)
            targets.append((abs(dx) + abs(dy), dx, dy, enemy))
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
        action = self._approach_direction(dx, dy, open_steps)
        if action:
            return PlannerDecision(action, f"close on enemy {enemy['slot']} at {distance}px",
                                   "engage_enemy")
        return None

    def _survive(self, observation, context, open_steps, critical=False):
        """Disengage and retrace the learned map toward the inn/safe anchor."""
        actor = context["actor"]
        dimensions = context.get("map_size_tiles", [0, 0])
        width_px, height_px = dimensions[0] * 16, dimensions[1] * 16
        enemies = [enemy for enemy in observation.get("enemy_slots", [])
                   if enemy.get("active")]
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
            return PlannerDecision(max(scored)[1],
                                   "retreat from enemies before returning to inn",
                                   "return_to_inn")

        anchor = self.history.safe_anchor() if self.history is not None else None
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
            routes = self._routes(position, cells, tuple(context["map_size_tiles"]))
            if target == position:
                if exit_action in open_steps:
                    return PlannerDecision(exit_action, "take learned exit toward inn",
                                           "return_to_inn", 1)
                return PlannerDecision("wait", "at learned inn/safe recovery point",
                                       "recover_at_inn")
            if target in routes:
                action, distance = routes[target]
                if action in open_steps:
                    return PlannerDecision(action,
                                           f"return to learned inn/safe point ({distance} tiles)",
                                           "return_to_inn", distance)

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

    def decide(self, observation, previous="none", directive="explore_unknown"):
        context = observation.get("tile_context") if isinstance(observation, dict) else None
        if not isinstance(context, dict) or not context.get("runtime_verified"):
            return PlannerDecision("wait", "no runtime-verified tile context", "observe")
        if context.get("game_mode") != "field":
            return PlannerDecision("wait", f"game mode is {context.get('game_mode')}", "observe")
        open_steps = self._open_steps(context)
        self._remember(observation, context)
        hero = (observation.get("party") or [{}])[0]
        hp, max_hp = hero.get("hp", 0), hero.get("max_hp", 0)
        survival = self._get_state("survival")
        if max_hp and hp * 2 <= max_hp:
            survival = {"active": True}
            self._set_state("survival", survival)
        if isinstance(survival, dict) and survival.get("active"):
            # Hysteresis prevents bouncing between fighting and retreating.
            # Resume only after a real recovery, not one lucky HP tick.
            if max_hp and hp * 10 >= max_hp * 9:
                self._set_state("survival", None)
            else:
                return self._survive(observation, context, open_steps,
                                     critical=bool(max_hp and hp * 4 <= max_hp))
        combat = self._combat(observation, context, open_steps, directive)
        if combat is not None:
            return combat
        if not open_steps:
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

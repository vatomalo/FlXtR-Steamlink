"""Persistent, fail-closed field planner for Secret of Mana."""
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


class MemoryPlanner:
    """Use verified WRAM, enemies and persisted visits to choose one safe action."""

    def __init__(self, history=None):
        self.history = history
        self.local_visits = {}
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

    def _remember(self, observation, context):
        if self.history is not None:
            self.history.remember_position(observation)
            return
        actor = context["actor"]
        key = (context["map_id"], actor["layer"], *context["actor_tile"])
        if key != self.last_position:
            self.local_visits[key] = self.local_visits.get(key, 0) + 1
            self.last_position = key

    def _visits(self, map_id, layer, x, y):
        if self.history is not None:
            return self.history.visit_count(map_id, layer, x, y)
        return self.local_visits.get((map_id, layer, x, y), 0)

    def _approach_enemy(self, observation, context, open_steps):
        actor = context["actor"]
        dimensions = context.get("map_size_tiles", [0, 0])
        width_px, height_px = dimensions[0] * 16, dimensions[1] * 16
        enemies = [enemy for enemy in observation.get("enemy_slots", [])
                   if enemy.get("active")]
        if not enemies:
            return None
        targets = []
        for enemy in enemies:
            dx = self._wrapped_delta(enemy["x_px"], actor["x_px"], width_px)
            dy = self._wrapped_delta(enemy["y_px"], actor["y_px"], height_px)
            targets.append((abs(dx) + abs(dy), dx, dy, enemy))
        distance, dx, dy, enemy = min(targets, key=lambda value: value[0])
        recharge = observation.get("party", [{}])[0].get("weapon_recharge")
        if (abs(dx) <= 48 and abs(dy) <= 48 and
                observation.get("weapons_enabled") and recharge == 0):
            return PlannerDecision("attack", f"enemy {enemy['slot']} in weapon range")
        axes = []
        if abs(dx) >= abs(dy):
            axes.extend(["step_right" if dx > 0 else "step_left",
                         "step_down" if dy > 0 else "step_up"])
        else:
            axes.extend(["step_down" if dy > 0 else "step_up",
                         "step_right" if dx > 0 else "step_left"])
        for action in axes:
            if action in open_steps and ((dx and action in ("step_left", "step_right")) or
                                         (dy and action in ("step_up", "step_down"))):
                return PlannerDecision(action, f"approach enemy {enemy['slot']} at {distance}px")
        return None

    def decide(self, observation, previous="none"):
        context = observation.get("tile_context") if isinstance(observation, dict) else None
        if not isinstance(context, dict) or not context.get("runtime_verified"):
            return PlannerDecision("wait", "no runtime-verified tile context")
        if context.get("game_mode") != "field":
            return PlannerDecision("wait", f"game mode is {context.get('game_mode')}")
        open_steps = self._open_steps(context)
        self._remember(observation, context)
        combat = self._approach_enemy(observation, context, open_steps)
        if combat is not None:
            return combat
        if not open_steps:
            return PlannerDecision("wait", "no verified open adjacent tile")

        map_id = context["map_id"]
        layer = context["actor"]["layer"]
        x, y = context["actor_tile"]
        width, height = context["map_size_tiles"]
        reverse = OPPOSITE_STEP.get(previous)
        order = {name: rank for rank, name in enumerate(
                 ("step_right", "step_down", "step_left", "step_up"))}
        scored = []
        for action in open_steps:
            dx, dy = STEP_OFFSETS[action]
            visits = self._visits(map_id, layer, (x + dx) % width, (y + dy) % height)
            scored.append((visits, action == reverse, action != previous,
                           order[action], action))
        visits, _, _, _, action = min(scored)
        return PlannerDecision(action, f"least-visited open tile ({visits} prior visits)")

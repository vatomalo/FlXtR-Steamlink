"""Short, contextual quips for the Secret of Mana AI speech overlay.

No model calls: dialogue must never stall the input loop. A fixed personality
chooses lines only when a genuine event occurs, with per-event cooldowns.
All output is printable ASCII and fits the SNES overlay's two short lines.
"""
import random
import time

LINES = {
    "curious": {
        "explore_frontier": ("I wonder what's ahead!", "Another path. Let's go!", "What lies beyond?"),
        "engage_enemy": ("Oh, a challenger!", "Let's see what you can do!"),
        "survive": ("This looks dangerous...", "Time to regroup!"),
        "return_to_inn": ("An inn sounds lovely.", "Rest before adventure!"),
        "stuck": ("Hmm. That's a wall.", "There must be another way!"),
        "map_change": ("A whole new place!", "Where are we now?"),
        "map_return": ("Back here again!", "I remember this place."),
        "victory": ("That went rather well!", "We did it!"),
        "hurt": ("Ow! That was rude!", "Careful now!"),
    },
    "bold": {
        "explore_frontier": ("Onward!", "No turning back!"),
        "engage_enemy": ("Come and get some!", "My turn!"),
        "survive": ("Tactical retreat!", "Not done yet!"),
        "return_to_inn": ("I'll be back stronger.",),
        "stuck": ("Move, stupid wall!", "A detour? Fine."),
        "map_change": ("New ground to conquer!",),
        "map_return": ("Back on familiar ground!",),
        "victory": ("Who's next?", "Too easy!"),
        "hurt": ("You'll regret that!",),
    },
    "dry": {
        "explore_frontier": ("Another suspicious path.", "Well, here we go."),
        "engage_enemy": ("Naturally. More monsters.",),
        "survive": ("This is going swimmingly.", "I prefer being alive."),
        "return_to_inn": ("A bed. Finally.",),
        "stuck": ("Excellent. A wall.", "The wall wins this round."),
        "map_change": ("A change of scenery.",),
        "map_return": ("This place again. Naturally.",),
        "victory": ("That was unnecessary.",),
        "hurt": ("How considerate.",),
    },
}
MIN_INTERVAL = 12.0
EVENT_COOLDOWN = 24.0


class Personality:
    def __init__(self, style="curious", seed=None, clock=None):
        if style not in LINES:
            raise ValueError("Unknown personality")
        self.style = style
        self.random = random.Random(seed)
        self.clock = clock or time.monotonic
        self.last_at = float("-inf")
        self.last_events = {}
        self.last_line = None
        self.previous_map = None
        self.previous_hp = None

    def observation_event(self, observation, visited_before=False):
        """Detect verified field-map changes and real HP loss."""
        if not isinstance(observation, dict):
            return None
        context = observation.get("tile_context") or {}
        if not context.get("runtime_verified") or context.get("game_mode") != "field":
            return None
        map_id = context.get("map_id")
        party = observation.get("party") or []
        hero = party[0] if party else {}
        hp = hero.get("hp")
        event = None
        if self.previous_map is not None and map_id != self.previous_map:
            event = "map_return" if visited_before else "map_change"
            self.previous_hp = None
        elif (self.previous_hp is not None and isinstance(hp, int)
              and hp < self.previous_hp and hp >= 0):
            event = "hurt"
        self.previous_map = map_id
        self.previous_hp = hp if isinstance(hp, int) else None
        return event

    def speak(self, event, now=None):
        if event not in LINES[self.style]:
            return None
        now = self.clock() if now is None else now
        if now - self.last_at < MIN_INTERVAL:
            return None
        if now - self.last_events.get(event, float("-inf")) < EVENT_COOLDOWN:
            return None
        choices = [s for s in LINES[self.style][event] if s != self.last_line]
        line = self.random.choice(choices or LINES[self.style][event])
        self.last_at = now
        self.last_events[event] = now
        self.last_line = line
        return line

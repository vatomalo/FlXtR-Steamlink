"""Short, contextual quips for the Secret of Mana AI speech overlay.

No model calls: dialogue must never stall the input loop. A fixed personality
chooses lines only when a genuine event occurs, with per-event cooldowns.
All output is printable ASCII and fits the SNES overlay's two short lines.
"""
import random
import time

# No apostrophes: the Steam Link message filter strips them ("whats").
# Every line fits one overlay line (32 characters).
LINES = {
    "curious": {
        "explore_frontier": (
            "I wonder what is ahead!", "Another path. Lets go!", "What lies beyond?",
            "Ooh, I have not been here.", "Lets see where this goes.", "A new trail? Yes please!",
            "Maybe treasure this way?", "Every step a new story.", "I love a good unknown.",
            "Is that a path? It is!"),
        "engage_enemy": (
            "Oh, a challenger!", "Lets see what you can do!", "Hello, little monster!",
            "Show me your moves!", "I have been practicing!", "Stand still, please!",
            "Here it comes!", "Ready when you are!", "This one looks feisty.",
            "Time to test my sword!"),
        "defend_enemy": (
            "Guard up!", "I see that swing coming.", "Not this time!", "Blocked, I think?",
            "Steady... steady...", "Shield up, then strike!", "I learned that trick!",
            "Wait for it..."),
        "kite_enemy": (
            "A step back, just in case.", "Let it come to me.", "Give me some room!",
            "Tactical backstep!", "Dancing out of reach!", "Not too close, friend."),
        "evade_enemy": (
            "Lets not fight that one.", "That one hits too hard.", "Nope, going around!",
            "Maybe another day.", "Sneaking past...", "Quietly, quietly..."),
        "cut_grass": (
            "Snip snip!", "Out of my way, grass!", "A little gardening.",
            "This grass is in the way.", "Trimming the path!", "Who planted this here?"),
        "survive": (
            "This looks dangerous...", "Time to regroup!", "Ouch. Retreat for now!",
            "I need a breather!", "Not my best moment.", "Back off, back off!"),
        "return_to_inn": (
            "An inn sounds lovely.", "Rest before adventure!", "A warm bed is calling.",
            "I could use a nap.", "Off to patch up!", "Soup and a bed, please!"),
        "go_to_inn": ("To the inn!", "Time for a good rest.", "Bed, here I come!",
                      "The innkeeper knows me."),
        "go_to_shop": ("Shopping time!", "Candy, here I come!", "Lets see the stock.",
                       "Money well spent soon.", "I hope they have Candy."),
        "go_to_map": ("Off to the hunting grounds!", "Lets go find monsters!",
                      "Back to the woods!", "Adventure awaits that way."),
        "leave_room": ("Lets head outside.", "Out the door we go!", "Fresh air time!"),
        "stuck": ("Hmm. That is a wall.", "There must be another way!", "Walls, my old enemy.",
                  "Lets try around.", "Okay, not that way."),
        "map_change": ("A whole new place!", "Where are we now?", "Ooh, scenery!",
                       "New map, new friends?", "This looks interesting!"),
        "map_return": ("Back here again!", "I remember this place.", "Familiar ground!",
                       "Hello again, old place.", "I know these paths."),
        "victory": ("That went rather well!", "We did it!", "Another one down!",
                    "Was that a level up?", "Good fight!"),
        "hurt": ("Ow! That was rude!", "Careful now!", "That stung!", "Hey, watch it!",
                 "Ouch, ouch, ouch!", "I felt that one.", "Rude!", "Not the face!"),
    },
    "bold": {
        "explore_frontier": ("Onward!", "No turning back!", "Forward, always!",
                             "The map fears me.", "Nothing stops me now!", "Charge ahead!",
                             "Lead on, feet!", "Fortune favors the bold!"),
        "engage_enemy": ("Come and get some!", "My turn!", "You picked the wrong hero!",
                         "Fight me!", "Bring it on!", "This ends now!", "Prepare yourself!",
                         "One swing is all I need!"),
        "defend_enemy": ("Hit me. I dare you.", "Blocked!", "Is that all?",
                         "My guard is iron!", "Nice try!", "Now my counter!"),
        "kite_enemy": ("Step back, strike back!", "Footwork wins fights!",
                       "Come closer, I insist.", "Baiting you in!"),
        "evade_enemy": ("Not worth my blade.", "I choose my battles.", "Later, beast!",
                        "You are lucky today."),
        "cut_grass": ("Grass, meet sword.", "Clearing the way!", "Nothing blocks me!",
                      "Hack and slash!"),
        "survive": ("Tactical retreat!", "Not done yet!", "I will be back!",
                    "Just catching my breath!", "Regroup, then revenge!"),
        "return_to_inn": ("I will be back stronger.", "Rest, then revenge!",
                          "Heal up, hit harder!"),
        "go_to_inn": ("A short rest. Then war!", "Inn break. Earned it."),
        "go_to_shop": ("Gear up!", "Supplies for the hunt!", "Stocking up!"),
        "go_to_map": ("To the hunt!", "Monsters, I am coming!", "Back to battle!"),
        "leave_room": ("Out! The fight is outside.", "Enough indoors!"),
        "stuck": ("Move, stupid wall!", "A detour? Fine.", "I will go around you!",
                  "Walls cannot hold me!"),
        "map_change": ("New ground to conquer!", "Mine now!", "Who rules here? Me!"),
        "map_return": ("Back on familiar ground!", "I own this place.",
                       "Remember me, monsters?"),
        "victory": ("Who is next?", "Too easy!", "Victory!", "Next!",
                    "Was that all you had?"),
        "hurt": ("You will regret that!", "Lucky shot!", "That all you got?",
                 "Now I am angry!", "A scratch!"),
    },
    "dry": {
        "explore_frontier": ("Another suspicious path.", "Well, here we go.",
                             "More trees. Thrilling.", "Exploring. Again.",
                             "I am sure this is fine.", "Onward, I suppose."),
        "engage_enemy": ("Naturally. More monsters.", "Oh good. Violence.",
                         "Lets get this over with.", "Another one. Lovely.",
                         "I had plans today."),
        "defend_enemy": ("Blocking. How exciting.", "I saw that coming.",
                         "Predictable.", "Guarding, as one does."),
        "kite_enemy": ("Personal space, please.", "Backing away. Gracefully.",
                       "No hugs, thank you."),
        "evade_enemy": ("I will pass, thanks.", "Not today, monster.",
                        "Avoidance is a skill."),
        "cut_grass": ("Gardening. My calling.", "The grass had it coming.",
                      "Lawn care, adventurer style."),
        "survive": ("This is going swimmingly.", "I prefer being alive.",
                    "Retreating. With dignity.", "Well, that went poorly."),
        "return_to_inn": ("A bed. Finally.", "Naptime is non-negotiable.",
                          "Inn. Sweet inn."),
        "go_to_inn": ("To the inn. Obviously.", "Sleep is a strategy."),
        "go_to_shop": ("Shopping. How heroic.", "Retail therapy.",
                       "Spending money. Joy."),
        "go_to_map": ("Back to the monsters.", "Work, work, work.",
                      "The woods again. Great."),
        "leave_room": ("Outside again. Hooray.", "Leaving. As planned."),
        "stuck": ("Excellent. A wall.", "The wall wins this round.",
                  "Walls. My nemesis.", "Of course it is blocked."),
        "map_change": ("A change of scenery.", "New place. Same me.",
                       "Somewhere else. Neat."),
        "map_return": ("This place again. Naturally.", "Ah. Here. Again.",
                       "Nothing changed. Shocking."),
        "victory": ("That was unnecessary.", "Moving on.", "One less problem.",
                    "Well. That happened."),
        "hurt": ("How considerate.", "Ow. Noted.", "That was uncalled for.",
                 "Wonderful. Pain."),
    },
}
MIN_INTERVAL = 8.0
EVENT_COOLDOWN = 16.0


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

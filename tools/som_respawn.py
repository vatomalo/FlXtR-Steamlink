"""Bounded autonomous advancement of the game's ordinary post-KO dialogue.

Never blindly confirm a generic event: this can also be a shop, a choice, or
story dialogue. A confirmed death -> transition -> recovered HP -> event
sequence is required. This does not load savestates, reset the game, or
modify WRAM. All commands still use the existing physical-takeover-safe proxy.
"""
import json
import time


class RespawnDialogue:
    def __init__(self, clock=None, max_confirms=10):
        self.clock = clock or time.monotonic
        self.max_confirms = max_confirms
        self.stage = "idle"
        self.last_event = None
        self.stable_since = None
        self.last_confirm_at = None
        self.confirms = 0
        self.finished = False

    def restore_recent(self, connection, max_age_seconds=300):
        """Rehydrate a witnessed KO sequence across a bridge service restart.

        Only the recent, actual WRAM observation history counts. Replaying a
        field observation clears a completed recovery, so unrelated story
        events cannot inherit a stale death authorization.
        """
        rows = connection.execute(
            "SELECT state FROM observations WHERE ts>=? "
            "ORDER BY id DESC LIMIT 900", (time.time() - max_age_seconds,)
        ).fetchall()
        for (encoded,) in reversed(rows):
            self.observe(json.loads(encoded))
        # No actions are sent by replay: current live observation is always
        # rechecked before the first button press.
        self.last_event = None
        self.stable_since = self.clock()
        self.last_confirm_at = None
        self.confirms = 0

    def observe(self, observation, event_byte=None):
        """Return a one-shot 'confirm' command, or None."""
        context = (observation or {}).get("tile_context") or {}
        mode = context.get("game_mode")
        hero = ((observation or {}).get("party") or [{}])[0] or {}
        status = hero.get("status") or {}
        now = self.clock()

        if mode in ("no_game", None):
            self.stage = "idle"
            return None
        if mode == "field":
            self.stage = "idle"
            self.finished = False
            self.confirms = 0
            return None

        dead = bool(status.get("verified") and
                    "dead" in (status.get("effects") or []) and
                    hero.get("hp") == 0)
        if dead:
            # The dead bit can persist across the transition for many frames.
            # Do not lose evidence that the respawn transition was witnessed.
            if mode == "transition" and self.stage in ("dead", "transition"):
                self.stage = "transition"
            elif self.stage != "transition":
                self.stage = "dead"
            self.last_event = None
            self.stable_since = None
            return None

        if self.stage == "dead" and mode == "transition":
            self.stage = "transition"
            return None
        if self.stage == "transition" and mode == "event" and \
                status.get("verified") and hero.get("hp", 0) > 0 and \
                "dead" not in (status.get("effects") or []):
            self.stage = "respawn_event"
            self.stable_since = now
            self.last_event = event_byte
            self.confirms = 0
            return None
        if self.stage != "respawn_event":
            return None
        if mode != "event" or not status.get("verified") or hero.get("hp", 0) <= 0:
            self.stage = "idle"
            return None

        if self.confirms >= self.max_confirms:
            if not self.finished:
                self.finished = True
            return None
        if event_byte != self.last_event:
            self.last_event = event_byte
            self.stable_since = now
            return None
        if self.stable_since is None:
            self.stable_since = now
            return None
        # Do not rush scripted respawn animations. Require a stable event
        # and at least 1s separation between discrete button edges.
        if now - self.stable_since < 1.5:
            return None
        if self.last_confirm_at is not None and now - self.last_confirm_at < 1.2:
            return None
        self.last_confirm_at = now
        self.confirms += 1
        return "confirm"

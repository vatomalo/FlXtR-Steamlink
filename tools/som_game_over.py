"""Notice a game over (hero died, game fell back to the title screen) and record it.

Read-only: this never presses a button. On the title screen the player picks
Continue and the save to load; the bridge only waits (the planner, the event
escape and the KO respawn handler all stay idle outside a loaded game).

A title screen counts as a death only when the hero was seen dead (verified
dead bit with HP 0) shortly before, and did not come back alive in between.
An ordinary Turbo KO auto-respawns the hero, which clears that evidence, so a
console reset or a bridge started on the title screen is never a death.

Each death is stored in the `deaths` table (map, nearest enemy, HP before the
fatal fight, play-loop phase) so the planner can treat that map as dangerous.
"""
import time

from som_memory import nearest_enemy_identity

# Seconds between the dead hero and the title screen that still link the two.
DEATH_WINDOW = 90.0


def ensure_table(connection):
    connection.execute(
        "CREATE TABLE IF NOT EXISTS deaths "
        "(ts REAL NOT NULL, map_id INTEGER, enemy_id INTEGER NOT NULL, "
        "hp_before INTEGER, max_hp INTEGER, phase TEXT)")
    connection.commit()


def deaths_by_map(connection):
    """{map_id: number of recorded game overs}; empty when nothing is recorded."""
    try:
        rows = connection.execute(
            "SELECT map_id,COUNT(*) FROM deaths WHERE map_id IS NOT NULL GROUP BY map_id")
        return {map_id: count for map_id, count in rows}
    except Exception:  # noqa: BLE001 - no deaths table yet (old or stub database)
        return {}


def _hero(observation):
    party = (observation or {}).get("party") or [{}]
    return party[0] if isinstance(party[0], dict) else {}


def _dead(hero):
    status = hero.get("status") or {}
    return bool(status.get("verified") and "dead" in (status.get("effects") or [])
                and hero.get("hp") == 0)


class GameOverWatch:
    def __init__(self, connection=None, clock=time.monotonic, window=DEATH_WINDOW):
        self.connection, self.clock, self.window = connection, clock, window
        if connection is not None:
            ensure_table(connection)
        self.alive = None        # last settled field sample with the hero alive
        self.died = None         # (time, alive sample before it) while dead
        self.on_title = False    # a game over was declared; waiting for Continue
        self.last = None         # the last recorded death

    def observe(self, observation, phase=None):
        """Return "game_over", "resumed" or None for this observation."""
        context = (observation or {}).get("tile_context") or {}
        mode = context.get("game_mode")
        hero = _hero(observation)
        now = self.clock()

        if mode == "no_game":
            if (self.died is not None and not self.on_title and
                    now - self.died[0] <= self.window):
                self.on_title = True
                self._record(self.died[1], phase)
                self.died = None
                return "game_over"
            return None

        if _dead(hero):
            if self.died is None:
                self.died = (now, self.alive)
            return None

        if (mode == "field" and not context.get("map_loading") and
                isinstance(hero.get("hp"), int) and hero["hp"] > 0):
            self.died = None     # respawned (ordinary KO) or never died
            self.alive = {"map_id": context.get("map_id"), "hp": hero["hp"],
                          "max_hp": hero.get("max_hp"),
                          "enemy_id": nearest_enemy_identity(observation)}
            if self.on_title:
                self.on_title = False
                return "resumed"
        return None

    def _record(self, before, phase):
        before = before or {}
        self.last = {"map_id": before.get("map_id"), "enemy_id": before.get("enemy_id", -1),
                     "hp_before": before.get("hp"), "max_hp": before.get("max_hp"),
                     "phase": phase}
        if self.connection is not None:
            self.connection.execute(
                "INSERT INTO deaths VALUES(?,?,?,?,?,?)",
                (time.time(), self.last["map_id"], self.last["enemy_id"],
                 self.last["hp_before"], self.last["max_hp"], phase))
            self.connection.commit()
            self.last["deaths_here"] = deaths_by_map(self.connection).get(self.last["map_id"], 0)

    def describe(self):
        last = self.last or {}
        enemy = (f"enemy type {last['enemy_id']:#04x}"
                 if isinstance(last.get("enemy_id"), int) and last["enemy_id"] >= 0
                 else "no enemy near")
        times = last.get("deaths_here")
        return (f"died on map {last.get('map_id')} ({enemy}, last HP "
                f"{last.get('hp_before')}/{last.get('max_hp')}, phase {last.get('phase')})"
                + (f"; {times} death(s) on that map so far" if times else ""))


# Turbo's game over never reaches the title screen: with the hero at HP 0
# the game shows its death text (event, D0 0x83), a menu (D0 1, cursor
# 7E:A211 0x40) and the load screens (transitions), each taking confirm,
# and the hero is back in the field with the save's HP (recorded from the
# player 2026-11-16: six confirms, ~1-5 s apart, slot 1).
LOAD_SETTLE_S = 1.0        # the screen must be unchanged this long before a press
LOAD_REPEAT_S = 3.0        # the same screen again only after this (a dropped press)
LOAD_PASSIVE_S = 6.0       # screens the game advances itself: only if stuck this long
LOAD_PRESS_GAP_S = 1.2     # at least this long between presses
LOAD_MAX_PRESSES = 12      # twice what the player needed; then stop and say so
# Death animation (event D0 0x81/0x82) and map fades (transition with E4 set)
# advanced without a press in the recording.
PASSIVE_EVENTS = (0x81, 0x82)
LOAD_WINDOW_S = 120.0      # a death this long ago no longer authorizes presses


class GameOverLoad:
    """Press confirm through the game-over screens until the save is loaded.

    Only while the hero is at HP 0 after being seen dying in the field, only
    on event and transition screens, one press per settled screen, at most
    LOAD_MAX_PRESSES. Done when the hero stands in the field with HP again.
    """

    def __init__(self, clock=time.monotonic):
        self.clock = clock
        self.died_at = None
        self.presses = 0
        self.screen = None         # (screen key, since)
        self.pressed_screen = None # (screen key, time) of the last press
        self.pressed_at = None
        self.gave_up = False

    @staticmethod
    def _screen(wram):
        return (wram.game_mode(), wram.u8(0x7E00D0), wram.u8(0x7E00E4), wram.u8(0x7E00E6),
                wram.u8(0x7EA211))

    def observe(self, observation, wram):
        """("confirm", reason), ("loaded", reason), ("stuck", reason) or None."""
        context = (observation or {}).get("tile_context") or {}
        mode = context.get("game_mode")
        hp = _hero(observation).get("hp")
        now = self.clock()
        if mode == "field" and isinstance(hp, int) and hp > 0:
            if self.died_at is not None and self.presses:
                done = ("loaded", f"save loaded after {self.presses} presses; HP {hp}")
                self.__init__(self.clock)
                return done
            self.__init__(self.clock)
            return None
        if hp != 0 or wram is None:
            return None
        if self.died_at is None:
            if mode != "field":
                return None             # only a death seen in the field arms it
            self.died_at = now
        if self.gave_up or now - self.died_at > LOAD_WINDOW_S:
            return None
        if mode not in ("event", "transition"):
            return None
        screen = self._screen(wram)
        if self.screen is None or self.screen[0] != screen:
            self.screen = (screen, now)
            return None
        passive = ((mode == "event" and screen[1] in PASSIVE_EVENTS) or
                   (mode == "transition" and screen[2] != 0))
        settle = LOAD_PASSIVE_S if passive else LOAD_SETTLE_S
        if self.pressed_screen is not None and self.pressed_screen[0] == screen:
            if now - self.pressed_screen[1] < LOAD_REPEAT_S:
                return None
        if (now - self.screen[1] < settle or
                (self.pressed_at is not None and now - self.pressed_at < LOAD_PRESS_GAP_S)):
            return None
        if self.presses >= LOAD_MAX_PRESSES:
            self.gave_up = True
            return ("stuck", f"still not loaded after {self.presses} presses")
        self.presses += 1
        self.pressed_at = now
        self.pressed_screen = (screen, now)
        return ("confirm", f"game over: confirm {self.presses}/{LOAD_MAX_PRESSES} "
                           f"on {screen[0]} screen (D0 {screen[1]:#04x})")

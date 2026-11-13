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

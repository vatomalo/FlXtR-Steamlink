#!/usr/bin/env python3
"""Persistent, deterministic world clock for the Secret of Mana living world.

Game time is a pure function of a stored anchor and the real time passed in:

    minutes = base_minutes + (real - anchor) * rate      (running)
    minutes = base_minutes                               (paused)

so the simulation never depends on how long an LLM or SSH round trip takes;
callers pass the real time explicitly (tests use a fake clock). The default
rate is one game hour per five real minutes (12 game minutes per real
minute). The state lives in its own SQLite table and survives restarts.

Catch-up: time that passed while nothing was running (game closed, service
stopped) is credited when the clock is opened again, up to
`max_catch_up_minutes` (default one game day), so a world left alone for a
week does not jump a week but still moves on.

See docs/SOM-LIVING-WORLD.md section "World clock".
"""
import sqlite3
import time

MINUTES_PER_DAY = 24 * 60
DEFAULT_RATE = 60.0 / 300.0          # game minutes per real second: 1 h / 5 min
DEFAULT_START = 6 * 60               # a new world starts at 06:00 on day 0
DEFAULT_MAX_CATCH_UP = MINUTES_PER_DAY
WEEKDAYS = ("moonday", "treeday", "waterday", "fireday", "windday", "earthday", "sunday")
# Phases by minute of day; the first matching upper bound wins.
PHASES = ((5 * 60, "night"), (7 * 60, "dawn"), (18 * 60, "day"), (20 * 60, "dusk"),
          (MINUTES_PER_DAY, "night"))


def parse_time(text):
    """'HH:MM' -> minute of day."""
    try:
        hours, minutes = text.strip().split(":")
        hours, minutes = int(hours), int(minutes)
    except (AttributeError, ValueError):
        raise ValueError(f"time must be HH:MM, got {text!r}") from None
    if not (0 <= hours < 24 and 0 <= minutes < 60):
        raise ValueError(f"time out of range: {text!r}")
    return hours * 60 + minutes


def format_time(minute_of_day):
    minute_of_day = int(minute_of_day) % MINUTES_PER_DAY
    return f"{minute_of_day // 60:02d}:{minute_of_day % 60:02d}"


def phase_of(minute_of_day):
    minute_of_day %= MINUTES_PER_DAY
    for bound, name in PHASES:
        if minute_of_day < bound:
            return name
    return "night"


class WorldTime:
    """An immutable instant of game time (whole minutes since day 0, 00:00)."""

    __slots__ = ("minutes",)

    def __init__(self, minutes):
        self.minutes = int(minutes)

    @property
    def day(self):
        return self.minutes // MINUTES_PER_DAY

    @property
    def minute_of_day(self):
        return self.minutes % MINUTES_PER_DAY

    @property
    def weekday(self):
        return WEEKDAYS[self.day % len(WEEKDAYS)]

    @property
    def phase(self):
        return phase_of(self.minute_of_day)

    @property
    def is_night(self):
        return self.phase == "night"

    def describe(self):
        return f"day {self.day} ({self.weekday}) {format_time(self.minute_of_day)} {self.phase}"

    def __eq__(self, other):
        return isinstance(other, WorldTime) and other.minutes == self.minutes

    def __hash__(self):
        return hash(self.minutes)

    def __repr__(self):
        return f"WorldTime({self.minutes}: {self.describe()})"


class WorldClock:
    """Game clock persisted in SQLite; every method takes the real time."""

    def __init__(self, connection, rate=None, start_minutes=DEFAULT_START,
                 max_catch_up_minutes=DEFAULT_MAX_CATCH_UP, real=None, key="world"):
        self.connection = connection
        self.key = key
        self.max_catch_up_minutes = max_catch_up_minutes
        connection.execute(
            "CREATE TABLE IF NOT EXISTS world_clock (key TEXT PRIMARY KEY, base_minutes REAL "
            "NOT NULL, anchor REAL NOT NULL, rate REAL NOT NULL, paused INTEGER NOT NULL, "
            "last_seen REAL NOT NULL)")
        real = time.time() if real is None else real
        row = connection.execute(
            "SELECT base_minutes, anchor, rate, paused, last_seen FROM world_clock WHERE key=?",
            (key,)).fetchone()
        if row is None:
            self.base, self.anchor = float(start_minutes), real
            self.rate = DEFAULT_RATE if rate is None else float(rate)
            self.paused, self.last_seen = False, real
        else:
            self.base, self.anchor, self.rate, paused, self.last_seen = row
            self.paused = bool(paused)
            self._catch_up(real)
            if rate is not None and float(rate) != self.rate:
                self.set_rate(rate, real)
        self._save(real)

    def _catch_up(self, real):
        """Credit time that passed while no process was running, capped."""
        if self.paused:
            return
        gap = max(0.0, real - self.last_seen)
        credited = min(gap * self.rate, float(self.max_catch_up_minutes))
        # Freeze at the last seen instant, then add the capped credit.
        self.base = self.base + (self.last_seen - self.anchor) * self.rate + credited
        self.anchor = real

    def _save(self, real):
        self.last_seen = real
        with self.connection:
            self.connection.execute(
                "INSERT OR REPLACE INTO world_clock VALUES (?,?,?,?,?,?)",
                (self.key, self.base, self.anchor, self.rate, int(self.paused), real))

    def minutes(self, real):
        if self.paused:
            return self.base
        return self.base + max(0.0, real - self.anchor) * self.rate

    def now(self, real=None):
        real = time.time() if real is None else real
        return WorldTime(int(self.minutes(real)))

    def checkpoint(self, real):
        """Record that the world was running at `real` (bounds later catch-up)."""
        self._save(real)

    def pause(self, real):
        if not self.paused:
            self.base, self.anchor, self.paused = self.minutes(real), real, True
        self._save(real)

    def resume(self, real):
        if self.paused:
            self.anchor, self.paused = real, False
        self._save(real)

    def set_rate(self, rate, real):
        rate = float(rate)
        if not 0.0 <= rate <= 60.0:
            raise ValueError("rate must be 0..60 game minutes per real second")
        self.base, self.anchor, self.rate = self.minutes(real), real, rate
        self._save(real)

    def set_time(self, minute_of_day, real, next_day=False):
        """Jump forward to the next occurrence of minute_of_day (never backward)."""
        current = self.minutes(real)
        day_start = (int(current) // MINUTES_PER_DAY) * MINUTES_PER_DAY
        target = day_start + minute_of_day
        if target < current or next_day:
            target += MINUTES_PER_DAY
        self.base, self.anchor = float(target), real
        self._save(real)


def open_clock(path, **options):
    connection = sqlite3.connect(path)
    return WorldClock(connection, **options)

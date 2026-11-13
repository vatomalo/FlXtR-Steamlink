import sqlite3
import unittest

from som_world_clock import (MINUTES_PER_DAY, WorldClock, format_time, parse_time,
                             phase_of)


class WorldClockTest(unittest.TestCase):
    def setUp(self):
        self.db = sqlite3.connect(":memory:")

    def test_default_rate_is_one_hour_per_five_minutes(self):
        clock = WorldClock(self.db, real=1000.0)
        self.assertEqual(clock.now(1000.0).minutes, 6 * 60)
        self.assertEqual(clock.now(1000.0 + 300).minutes, 7 * 60)

    def test_deterministic_for_the_same_real_time(self):
        clock = WorldClock(self.db, real=0.0)
        self.assertEqual(clock.now(1234.5), clock.now(1234.5))

    def test_pause_and_resume(self):
        clock = WorldClock(self.db, real=0.0)
        clock.pause(300.0)
        self.assertEqual(clock.now(10_000.0).minutes, 7 * 60)
        clock.resume(10_000.0)
        self.assertEqual(clock.now(10_300.0).minutes, 8 * 60)

    def test_persists_and_catches_up_with_a_cap(self):
        clock = WorldClock(self.db, real=0.0, max_catch_up_minutes=120)
        clock.checkpoint(300.0)                                  # 07:00
        reopened = WorldClock(self.db, real=300.0 + 3600.0, max_catch_up_minutes=120)
        self.assertEqual(reopened.now(3900.0).minutes, 7 * 60 + 120)
        short = WorldClock(sqlite3.connect(":memory:"), real=0.0)
        short.checkpoint(0.0)

    def test_short_gap_is_credited_fully(self):
        WorldClock(self.db, real=0.0).checkpoint(0.0)
        reopened = WorldClock(self.db, real=600.0)
        self.assertEqual(reopened.now(600.0).minutes, 8 * 60)

    def test_paused_clock_does_not_catch_up(self):
        clock = WorldClock(self.db, real=0.0)
        clock.pause(0.0)
        self.assertEqual(WorldClock(self.db, real=99_999.0).now(99_999.0).minutes, 6 * 60)

    def test_rate_change_keeps_current_time(self):
        clock = WorldClock(self.db, real=0.0)
        clock.set_rate(1.0, 300.0)
        self.assertEqual(clock.now(300.0).minutes, 7 * 60)
        self.assertEqual(clock.now(360.0).minutes, 7 * 60 + 60)
        with self.assertRaises(ValueError):
            clock.set_rate(-1, 400.0)

    def test_set_time_only_moves_forward(self):
        clock = WorldClock(self.db, real=0.0)
        clock.set_time(parse_time("21:00"), 0.0)
        self.assertEqual(clock.now(0.0).minutes, 21 * 60)
        clock.set_time(parse_time("06:00"), 0.0)
        now = clock.now(0.0)
        self.assertEqual((now.day, now.minute_of_day), (1, 6 * 60))

    def test_phases_weekdays_and_parsing(self):
        self.assertEqual(phase_of(parse_time("04:59")), "night")
        self.assertEqual(phase_of(parse_time("06:00")), "dawn")
        self.assertEqual(phase_of(parse_time("12:00")), "day")
        self.assertEqual(phase_of(parse_time("19:00")), "dusk")
        self.assertEqual(phase_of(parse_time("23:00")), "night")
        self.assertEqual(format_time(MINUTES_PER_DAY + 75), "01:15")
        clock = WorldClock(self.db, real=0.0)
        clock.set_time(0, 0.0, next_day=True)
        self.assertNotEqual(clock.now(0.0).weekday, None)
        for bad in ("24:00", "7", "aa:bb", None):
            with self.assertRaises(ValueError):
                parse_time(bad)


if __name__ == "__main__":
    unittest.main()

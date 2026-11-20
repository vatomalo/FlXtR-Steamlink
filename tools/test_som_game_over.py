import sqlite3
import unittest

import som_play_loop as loop
from som_game_over import GameOverWatch, deaths_by_map


def obs(mode, hp=50, dead=False, map_id=131):
    effects = ["dead"] if dead else []
    return {"tile_context": {"game_mode": mode, "map_id": map_id,
                             "actor": {"x_px": 100, "y_px": 100}},
            "party": [{"hp": 0 if dead else hp, "max_hp": 100,
                       "status": {"verified": True, "effects": effects}}],
            "enemy_slots": [{"active": True, "identity": 0x0a, "x_px": 120, "y_px": 100}]}


class Clock:
    def __init__(self):
        self.now = 0.0

    def __call__(self):
        return self.now


class GameOverWatchTest(unittest.TestCase):
    def setUp(self):
        self.db = sqlite3.connect(":memory:")
        self.clock = Clock()
        self.watch = GameOverWatch(self.db, clock=self.clock)

    def test_death_then_title_is_recorded_and_resume_is_reported(self):
        self.assertIsNone(self.watch.observe(obs("field", hp=12)))
        self.assertIsNone(self.watch.observe(obs("field", dead=True)))
        self.assertIsNone(self.watch.observe(obs("transition", dead=True)))
        self.clock.now = 10
        self.assertEqual(self.watch.observe(obs("no_game"), "hunt"), "game_over")
        self.assertIsNone(self.watch.observe(obs("no_game")))   # once only
        row = self.db.execute("SELECT map_id,enemy_id,hp_before,max_hp,phase FROM deaths").fetchall()
        self.assertEqual(row, [(131, 0x0a, 12, 100, "hunt")])
        self.assertIn("died on map 131", self.watch.describe())
        self.assertEqual(self.watch.observe(obs("field", hp=100, map_id=40)), "resumed")
        self.assertIsNone(self.watch.observe(obs("field", hp=100, map_id=40)))

    def test_ko_with_respawn_then_reset_is_not_a_death(self):
        self.watch.observe(obs("field", hp=5))
        self.watch.observe(obs("field", dead=True))
        self.watch.observe(obs("field", hp=100))                # Turbo respawn
        self.assertIsNone(self.watch.observe(obs("no_game")))
        self.assertEqual(deaths_by_map(self.db), {})

    def test_starting_on_title_or_late_title_is_not_a_death(self):
        self.assertIsNone(self.watch.observe(obs("no_game")))
        self.watch.observe(obs("field", hp=5))
        self.watch.observe(obs("field", dead=True))
        self.clock.now = 1000
        self.assertIsNone(self.watch.observe(obs("no_game")))

    def test_deaths_push_a_map_down_the_hunting_ranking(self):
        class History:
            connection = self.db
        self.db.execute("CREATE TABLE reward_ledger (map_id, gold_gained, hp_lost)")
        self.db.executemany("INSERT INTO reward_ledger VALUES (?,?,?)",
                            [(131, 300, 100), (40, 100, 100)])
        self.assertEqual(loop.best_hunt_map(History, lambda m: True), 131)
        self.watch.observe(obs("field", hp=5, map_id=131))
        self.watch.observe(obs("field", dead=True, map_id=131))
        self.watch.observe(obs("no_game"))
        self.assertEqual(loop.best_hunt_map(History, lambda m: True), 40)


if __name__ == "__main__":
    unittest.main()


class FakeScreen:
    """WRAM stand-in for GameOverLoad: game mode plus the screen bytes."""

    def __init__(self, mode, d0=0, e4=1, e6=0x0F, cursor=0):
        self.mode, self.bytes = mode, {0x7E00D0: d0, 0x7E00E4: e4, 0x7E00E6: e6,
                                       0x7EA211: cursor, 0x7EE000: 1}

    def game_mode(self):
        return self.mode

    def u8(self, address):
        return self.bytes.get(address, 0)


def load_observation(mode, hp):
    return {"party": [{"hp": hp, "max_hp": 127}], "tile_context": {"game_mode": mode}}


class GameOverLoadTest(unittest.TestCase):
    def play(self, load, clock, screens):
        """screens: [(mode, hp, FakeScreen, seconds)] -> results in order."""
        results = []
        for mode, hp, wram, seconds in screens:
            end = clock.now + seconds
            while clock.now < end:
                result = load.observe(load_observation(mode, hp), wram)
                if result and result[0] != "hold":
                    results.append(result[0])
                clock.now += 0.25
        return results

    def test_confirms_through_the_recorded_game_over_then_reports_loaded(self):
        from som_game_over import GameOverLoad
        clock = Clock()
        load = GameOverLoad(clock)
        results = self.play(load, clock, [
            ("field", 0, FakeScreen("field"), 1),
            ("event", 0, FakeScreen("event", 0x82), 3),           # death animation
            ("transition", 0, FakeScreen("transition", 1, 3, 11), 2),   # map fade
            ("event", 0, FakeScreen("event", 0x83), 1.5),         # death text
            ("event", 0, FakeScreen("event", 1, cursor=0x40), 1.5),     # menu
            ("transition", 0, FakeScreen("transition", 0, 0, 0, 1), 1.5),  # load screen
            ("transition", 0, FakeScreen("transition", 0, 0, 0, 0), 1.5),
            ("field", 127, FakeScreen("field"), 1),
        ])
        self.assertEqual(results, ["confirm"] * 4 + ["loaded"])

    def test_never_presses_while_the_hero_has_hp(self):
        from som_game_over import GameOverLoad
        clock = Clock()
        load = GameOverLoad(clock)
        self.assertEqual(self.play(load, clock, [
            ("event", 90, FakeScreen("event", 0x83), 5),           # a story dialogue
            ("transition", 90, FakeScreen("transition", 0, 0, 0), 5),
        ]), [])

    def test_arms_when_hp_reaches_zero_inside_the_death_event(self):
        # Live 16 Nov: the last field sample had 4 HP; 0 appeared in the event.
        from som_game_over import GameOverLoad
        clock = Clock()
        load = GameOverLoad(clock)
        results = self.play(load, clock, [
            ("field", 4, FakeScreen("field"), 1),
            ("event", 0, FakeScreen("event", 0x83), 1.5),
            ("transition", 0, FakeScreen("transition", 0, 0, 0, 0x40), 1.5),
            ("field", 127, FakeScreen("field"), 1)])
        self.assertEqual(results, ["confirm", "confirm", "loaded"])

    def test_holds_the_controls_between_presses(self):
        from som_game_over import GameOverLoad
        clock = Clock()
        load = GameOverLoad(clock)
        result = load.observe(load_observation("event", 0), FakeScreen("event", 0x82))
        self.assertEqual(result[0], "hold")

    def test_gives_up_after_the_press_limit(self):
        from som_game_over import GameOverLoad, LOAD_MAX_PRESSES
        clock = Clock()
        load = GameOverLoad(clock)
        results = self.play(load, clock, [("field", 0, FakeScreen("field"), 1),
                                          ("event", 0, FakeScreen("event", 0x83), 120)])
        self.assertEqual(results, ["confirm"] * LOAD_MAX_PRESSES + ["stuck"])

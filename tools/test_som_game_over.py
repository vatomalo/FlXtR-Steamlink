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

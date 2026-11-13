"""Host-only, synthetic tests of persistent online combat experience."""
import sqlite3
import unittest

from som_learning import CombatLearner, eligible


def state(enemy_hp=40, hero_hp=100, distance=30, map_id=130, enemy_id=0,
          verified=True, allies=False):
    return {
        "map_id": map_id, "weapons_enabled": True,
        "tile_context": {"runtime_verified": verified, "game_mode": "field",
                         "map_id": map_id, "map_size_tiles": [128, 128],
                         "actor": {"x_px": 100, "y_px": 100}},
        "party": [
            {"hp": hero_hp, "max_hp": 100, "hp_plausible": True,
             "weapon": {"verified": True, "type": "sword"}},
            {"availability_byte": 1 if allies else 0}],
        "enemy_slots": [
            {"slot": 1, "active": True, "hp_plausible": True, "hp": enemy_hp,
             "identity": enemy_id, "x_px": 100 + distance, "y_px": 100}]
    }


class TestLearning(unittest.TestCase):
    def setUp(self):
        self.now = 0.0
        self.connection = sqlite3.connect(":memory:")
        self.learner = CombatLearner(self.connection, clock=lambda: self.now)

    def tearDown(self):
        self.connection.close()

    def test_successful_attack_is_persisted(self):
        self.assertTrue(self.learner.start(state()))
        self.now += 0.2
        result = self.learner.observe(state(enemy_hp=32, hero_hp=98))
        self.assertEqual(result["damage"], 8)
        self.assertEqual(result["hp_lost"], 2)
        self.assertEqual(self.learner.summary(0, "sword")[0]["hits"], 1)

    def test_miss_needs_full_window(self):
        self.learner.start(state())
        self.now = 0.4
        self.assertIsNone(self.learner.observe(state()))
        self.now = 0.9
        result = self.learner.observe(state())
        self.assertFalse(result["hit"])

    def test_ambiguous_outcomes_are_discarded(self):
        self.assertFalse(self.learner.start(state(allies=True)))
        self.learner.start(state())
        self.now = 0.2
        self.assertIsNone(self.learner.observe(state(enemy_hp=0, verified=False)))
        self.assertEqual(self.learner.summary(0, "sword"), [])
        self.learner.start(state())
        self.assertIsNone(self.learner.observe(state(map_id=131, enemy_hp=20)))
        self.assertEqual(self.learner.summary(0, "sword"), [])

    def test_learned_range_needs_comparative_evidence(self):
        self.assertFalse(self.learner.should_close(0, "sword", 37))
        for bucket, attempts, hits in [(1, 5, 4), (2, 4, 0)]:
            self.connection.execute(
                "INSERT INTO learned_attack_ranges VALUES(?,?,?,?,?,?,?)",
                (0, "sword", bucket, attempts, hits, hits * 8, 0))
        self.connection.commit()
        self.assertTrue(self.learner.should_close(0, "sword", 37))
        self.assertFalse(self.learner.should_close(0, "sword", 21))
        self.assertFalse(self.learner.should_close(1, "sword", 37))
        self.assertFalse(self.learner.should_close(0, "glove", 37))

    def test_exploration_keeps_learning_possible(self):
        # Eight attempts total -> permit a retry of the poorly scoring range.
        self.connection.execute("INSERT INTO learned_attack_ranges VALUES(0,'sword',1,4,4,40,0)")
        self.connection.execute("INSERT INTO learned_attack_ranges VALUES(0,'sword',2,4,0,0,0)")
        self.connection.commit()
        self.assertFalse(self.learner.should_close(0, "sword", 37, 1.0))
        self.assertTrue(self.learner.should_close(0, "sword", 37, 0.3))


if __name__ == "__main__":
    unittest.main()

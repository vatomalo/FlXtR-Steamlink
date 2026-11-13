"""Reward ledger tests: GP gained is rewarded, HP lost is penalized."""
import tempfile
import unittest
from pathlib import Path

from som_memory import ObservationHistory
from som_planner import MemoryPlanner


def sample(hp=100, max_hp=100, map_id=129, enemy=None, mode="field", loading=False):
    return {"party": [{"hp": hp, "max_hp": max_hp, "hp_plausible": True,
                       "weapon_recharge": 20}],
            "weapons_enabled": True,
            "enemy_slots": [enemy] if enemy else [],
            "tile_context": {
                "runtime_verified": mode == "field" and not loading, "game_mode": mode,
                "map_loading": loading, "radius": 1, "map_id": map_id,
                "map_size_tiles": [8, 8], "actor_tile": [2, 3],
                "actor": {"x_px": 40, "y_px": 56, "layer": 1},
                "passability_rows": ["...", ".@.", "..."]}}


RABITE = {"slot": 1, "active": True, "identity": 0x00, "x_px": 72, "y_px": 56}


class TestRewardLedger(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.history = ObservationHistory(Path(self.directory.name) / "reward.sqlite")

    def tearDown(self):
        self.history.close()
        self.directory.cleanup()

    def test_hp_loss_is_penalized_and_gp_gain_rewarded(self):
        self.assertIsNone(self.history.record_reward(sample(), 500))
        hit = self.history.record_reward(sample(hp=90, enemy=RABITE), 500)
        self.assertEqual((hit["hp_lost"], hit["gold_gained"], hit["enemy_id"]), (10, 0, 0))
        self.assertAlmostEqual(hit["score"], -20.0)        # 10 % of max HP x 2
        paid = self.history.record_reward(sample(hp=90, enemy=RABITE), 530)
        self.assertEqual((paid["gold_gained"], paid["score"]), (30, 30.0))
        self.assertEqual(self.history.reward_summary(), ["map 129: +30GP -10HP score 10"])

    def test_healing_spending_and_misreads_score_nothing(self):
        self.history.record_reward(sample(hp=50), 500)
        self.assertIsNone(self.history.record_reward(sample(hp=100), 400))     # inn, shop
        self.assertIsNone(self.history.record_reward(sample(hp=100), 90000))   # misread
        self.assertIsNone(self.history.record_reward(sample(hp=100), None))    # uncalibrated
        # Uncalibrated gold still penalizes HP loss.
        self.assertEqual(self.history.record_reward(sample(hp=80), None)["score"], -40.0)

    def test_unsettled_samples_and_level_ups_break_the_comparison(self):
        self.history.record_reward(sample(hp=100), 500)
        self.assertIsNone(self.history.record_reward(sample(hp=60, loading=True), 500))
        self.assertIsNone(self.history.record_reward(sample(hp=60), 500))
        self.assertIsNone(self.history.record_reward(sample(hp=60, mode="event"), 500))
        self.history.record_reward(sample(hp=60), 500)
        self.assertIsNone(self.history.record_reward(sample(hp=50, max_hp=120), 500))

    def test_enemy_type_that_costs_more_hp_than_gp_is_avoided(self):
        self.history.record_reward(sample(), 0)
        for _ in range(5):
            self.history.record_reward(sample(hp=50, enemy=RABITE), 0)
            self.history.record_reward(sample(hp=100), 0)    # healed in between
        self.assertEqual(self.history.unprofitable_enemies(), {0x00})
        # Paying enough GP makes the same type worth fighting again.
        self.history.record_reward(sample(hp=100, enemy=RABITE), 600)
        self.assertEqual(self.history.unprofitable_enemies(), set())


class TestRewardDrivenCombat(unittest.TestCase):
    def test_backs_away_from_unprofitable_enemy_and_ignores_it_farther_off(self):
        planner = MemoryPlanner()
        planner.local_avoided = {0x00}
        decision = planner.decide(sample(enemy=RABITE))
        self.assertEqual((decision.action, decision.intention), ("step_left", "evade_enemy"))
        far = dict(RABITE, x_px=40 + 120)
        self.assertNotEqual(planner.decide(sample(enemy=far)).intention, "engage_enemy")
        other = dict(RABITE, identity=0x05)
        self.assertEqual(planner.decide(sample(enemy=other)).intention, "engage_enemy")


if __name__ == "__main__":
    unittest.main()

import sqlite3
import unittest

import som_block as block


def obs(hp, enemy_px=(56, 56), identity=7, max_hp=100):
    enemies = [] if enemy_px is None else [
        {"active": True, "x_px": enemy_px[0], "y_px": enemy_px[1], "identity": identity}]
    return {"party": [{"hp": hp, "max_hp": max_hp}], "enemy_slots": enemies,
            "tile_context": {"actor": {"x_px": 40, "y_px": 56}}}


class BlockLearnerTest(unittest.TestCase):
    def setUp(self):
        self.db = sqlite3.connect(":memory:")
        self.addCleanup(self.db.close)
        self.logs = []
        self.learner = block.BlockLearner(self.db, self.logs.append)

    def feed(self, action, losses, identity=7):
        hp = 100
        self.learner.record(obs(hp, identity=identity), action)
        for lost in losses:
            hp -= lost
            self.learner.record(obs(hp, identity=identity), action)
        self.learner.record(obs(hp, enemy_px=None), "wait")   # settle, close nothing

    def test_explores_until_both_sides_have_samples(self):
        self.assertTrue(self.learner.should_block(7))           # 0 vs 0: try blocking
        self.feed("block", [0] * 3)
        self.assertFalse(self.learner.should_block(7))          # 4 blocked vs 0 open
        self.feed("attack", [0] * 5)
        self.assertTrue(self.learner.should_block(7))

    def test_blocks_only_when_it_reduces_damage(self):
        self.feed("block", [1] * 10)
        self.feed("attack", [6] * 10)
        self.assertTrue(self.learner.should_block(7))
        self.assertTrue(any("blocking pays" in line for line in self.logs))
        self.feed("block", [9] * 40, identity=8)
        self.feed("attack", [5] * 40, identity=8)
        self.assertFalse(self.learner.should_block(8))

    def test_ignores_far_enemies_heals_and_ko(self):
        self.learner.record(obs(100, enemy_px=(300, 300)), "block")
        self.assertIsNone(self.learner.pending)
        self.learner.record(obs(50), "block")
        self.learner.record(obs(90), "block")                     # healed: ignored
        self.learner.record(obs(0), "block")                      # KO: ignored
        self.assertEqual(self.learner.stats(7)[True], (0, 0))

    def test_unknown_identity_never_blocks(self):
        self.assertFalse(self.learner.should_block(None))

    def test_persists(self):
        self.feed("block", [2, 2])
        again = block.BlockLearner(self.db, self.logs.append)
        self.assertEqual(again.stats(7)[True], (3, 4))


if __name__ == "__main__":
    unittest.main()

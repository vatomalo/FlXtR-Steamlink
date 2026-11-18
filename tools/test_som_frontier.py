import tempfile, unittest
from pathlib import Path

from som_frontier import EASY_MIN_SAMPLES, FRONTIER_TRIES, OVERVISITED, Frontier
from som_memory import ObservationHistory


class TestFrontier(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.history = ObservationHistory(Path(self.directory.name) / "f.sqlite")
        self.frontier = Frontier(self.history, log=lambda line: None)

    def tearDown(self):
        self.history.close()
        self.directory.cleanup()

    def edge(self, a, b):
        for f, t in ((a, b), (b, a)):
            self.history.connection.execute(
                "INSERT INTO map_edges VALUES(?,?,1,0,0,'step_left',1,0)", (f, t))

    def visit(self, map_id, visits=10):
        self.history.connection.execute(
            "INSERT INTO tile_visits VALUES(?,1,0,0,?,0)", (map_id, visits))

    def fights(self, map_id, count, taken):
        self.history.connection.executemany(
            "INSERT INTO combat_samples(ts,map_id,enemy_slot,action,distance_px,hero_hp,"
            "enemy_hp,damage_dealt,damage_taken,hero_level,enemy_id) "
            "VALUES(0,?,0,'attack',10,100,10,1,?,9,1)", [(map_id, taken)] * count)

    def test_new_corridor_beats_known_and_dead_end(self):
        # 1 (here) - 2 known - 3 new corridor - 4 ; 1 - 5 new dead end
        for a, b in ((1, 2), (2, 3), (3, 4), (1, 5)):
            self.edge(a, b)
        self.visit(1); self.visit(2)
        self.assertEqual(self.frontier.choose(1, 100)[0], 3)

    def test_too_easy_and_overvisited_maps_are_skipped(self):
        self.edge(1, 2); self.edge(1, 3); self.edge(1, 4)
        self.visit(1); self.visit(2); self.visit(3, OVERVISITED); self.visit(4)
        self.fights(2, EASY_MIN_SAMPLES, 0.0)          # nothing hurts here
        self.fights(4, 50, 5.0)                        # still dangerous enough
        self.fights(1, EASY_MIN_SAMPLES, 0.0)
        self.assertEqual(self.frontier.choose(1, 100)[0], 4)
        self.assertIn("too easy", Frontier.describe(self.history, 2, 100))

    def test_unreachable_frontier_is_given_up(self):
        self.edge(1, 2); self.edge(1, 3)
        self.visit(1, OVERVISITED); self.visit(3); self.fights(3, 10, 5.0)
        for _ in range(FRONTIER_TRIES):
            self.assertEqual(self.frontier.choose(1, 100)[0], 2)
            self.frontier.failed(2, "story gate")
        self.assertEqual(self.frontier.choose(1, 100)[0], 3)

    def test_nothing_left_falls_back(self):
        self.visit(1, OVERVISITED)
        self.assertIsNone(self.frontier.choose(1, 100))


if __name__ == "__main__":
    unittest.main()

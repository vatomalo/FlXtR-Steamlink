"""Host-only, synthetic tests of persistent online combat experience."""
import sqlite3
import json
import tempfile
import unittest
from pathlib import Path
from unittest.mock import Mock, patch

from som_learning import CombatLearner, eligible


def state(enemy_hp=40, hero_hp=100, distance=30, map_id=130, enemy_id=0,
          verified=True, allies=False, recharge=0):
    return {
        "map_id": map_id, "weapons_enabled": True, "pad_bindings": [1, 0, 0],
        "tile_context": {"runtime_verified": verified, "game_mode": "field",
                         "map_id": map_id, "map_size_tiles": [128, 128],
                         "actor": {"x_px": 100, "y_px": 100}},
        "party": [
            {"hp": hero_hp, "max_hp": 100, "hp_plausible": True,
             "availability_byte": 1, "graphic": "boy", "controller_binding": 1,
             "weapon_recharge": recharge, "status": {"verified": True, "raw": 0},
             "weapon": {"verified": True, "type": "sword", "charge_stage": 0}},
            {"availability_byte": 1 if allies else 0}, {"availability_byte": 0}],
        "enemy_slots": [
            {"slot": 1, "active": True, "active_byte": 1, "status": 0,
             "hp_plausible": True, "hp": enemy_hp, "max_hp": 50,
             "identity": enemy_id, "x_px": 100 + distance, "y_px": 100},
            {"slot": 2, "active": False, "active_byte": 0},
            {"slot": 3, "active": False, "active_byte": 0}]
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
        result = self.learner.observe(state(enemy_hp=32, hero_hp=98, recharge=50))
        self.assertEqual(result["damage"], 8)
        self.assertEqual(result["hp_lost"], 2)
        self.assertEqual(self.learner.summary(0, "sword")[0]["hits"], 1)

    def test_miss_needs_full_window(self):
        self.learner.start(state())
        self.now = 0.4
        self.assertIsNone(self.learner.observe(state(recharge=50)))
        self.now = 0.9
        result = self.learner.observe(state(recharge=20))
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
        # The counter advances even when no attacks/outcomes are added.
        self.connection.execute("INSERT INTO learned_attack_ranges VALUES(0,'sword',1,4,4,40,0)")
        self.connection.execute("INSERT INTO learned_attack_ranges VALUES(0,'sword',2,4,0,0,0)")
        self.connection.commit()
        choices = [self.learner.should_close(0, "sword", 37, 1.0) for _ in range(16)]
        self.assertEqual(choices, [True] * 7 + [False] + [True] * 7 + [False])
        self.assertTrue(self.learner.should_close(0, "sword", 37, 0.3))

    def test_command_or_damage_without_execution_does_not_train(self):
        self.learner.start(state())
        self.now = .9
        self.assertIsNone(self.learner.observe(state(enemy_hp=30)))
        self.now = 2
        self.assertIsNone(self.learner.observe(state(enemy_hp=30)))
        self.assertEqual(self.learner.summary(0, "sword"), [])

    def test_repeated_snapshot_cannot_become_a_miss(self):
        self.learner.start(state())
        self.now = .2
        self.learner.observe(state(recharge=50))
        self.now = .9
        self.assertIsNone(self.learner.observe(state(recharge=50)))
        self.now = 2
        self.learner.observe(state(recharge=50))
        self.assertEqual(self.learner.summary(0, "sword"), [])

    def test_completed_attempt_and_replayed_id_only_score_once(self):
        self.learner.start(state(), attempt_id="command-1")
        self.now = .3
        result = self.learner.observe(state(enemy_hp=32, recharge=50))
        self.assertTrue(result["hit"])
        self.assertIsNone(self.learner.observe(state(enemy_hp=32, recharge=50)))
        self.assertFalse(self.learner.start(state(), attempt_id="command-1"))
        row = self.connection.execute(
            "SELECT result,before_state,after_state FROM combat_learning_attempts").fetchone()
        self.assertEqual(row[0], "hit")
        self.assertEqual(json.loads(row[1])["hp"] - json.loads(row[2])["hp"], 8)
        self.assertEqual(self.learner.summary(0, "sword")[0]["attempts"], 1)

    def test_identity_changes_disappearance_healing_and_companions_discard(self):
        cases = [state(enemy_id=1), state(map_id=131), state(allies=True),
                 state(enemy_hp=45), state(enemy_hp=0), state(verified=False)]
        changed = state()
        changed["enemy_slots"][0]["max_hp"] = 60
        cases.append(changed)
        for after in cases:
            self.assertTrue(self.learner.start(state()))
            self.now += .2
            self.assertIsNone(self.learner.observe(after))
            self.assertIsNone(self.learner.pending)
        self.assertEqual(self.learner.summary(0, "sword"), [])

    def test_partial_hp_recovery_is_not_hidden_below_initial_hp(self):
        self.learner.start(state())
        self.now = .2
        self.learner.observe(state(enemy_hp=30))
        self.now = .4
        self.assertIsNone(self.learner.observe(state(enemy_hp=35, recharge=50)))
        self.assertIsNone(self.learner.pending)

    def test_invalid_inputs_fail_closed(self):
        cases = [None, [], "bad", {}, {"party": "bad"}, {"tile_context": [1]}]
        for path, value in [(("party", 0, "controller_binding"), 0),
                            (("party", 0, "graphic"), "girl"),
                            (("party", 0, "weapon_recharge"), "bad"),
                            (("party", 0, "weapon", "charge_stage"), 1),
                            (("party", 0, "status", "raw"), 0x2000),
                            (("enemy_slots", 0, "x_px"), None),
                            (("enemy_slots", 0, "status"), 0x2000),
                            (("enemy_slots", 1, "active_byte"), 1),
                            (("tile_context", "map_loading"), True),
                            (("weapons_enabled",), False)]:
            observation = state()
            target = observation
            for key in path[:-1]:
                target = target[key]
            target[path[-1]] = value
            cases.append(observation)
        for observation in cases:
            with self.subTest(observation=observation):
                self.assertIsNone(eligible(observation))

    def test_not_ready_and_intervening_actions_do_not_train(self):
        self.assertFalse(self.learner.start(state(recharge=50)))
        for action in ("charge", "block", "confirm", "open_ring"):
            self.learner.action_sent("attack", state())
            self.learner.action_sent(action)
            self.now += .2
            self.assertIsNone(self.learner.observe(state(enemy_hp=20, recharge=40)))
        self.assertEqual(self.learner.summary(0, "sword"), [])

    def test_backward_clock_and_timeout_discard_even_when_damage_arrives(self):
        for elapsed in (-1, 2):
            self.now = 0
            self.learner.start(state())
            self.now = elapsed
            self.assertIsNone(self.learner.observe(state(enemy_hp=20, recharge=50)))
        self.assertEqual(self.learner.summary(0, "sword"), [])

    def test_restart_preserves_learning_and_practice_schedule(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "history.sqlite"
            db = sqlite3.connect(path)
            learner = CombatLearner(db, clock=lambda: self.now)
            db.execute("INSERT INTO learned_attack_ranges VALUES(0,'sword',1,5,5,40,0)")
            db.execute("INSERT INTO learned_attack_ranges VALUES(0,'sword',2,4,0,0,0)")
            db.commit()
            for _ in range(7):
                self.assertTrue(learner.should_close(0, "sword", 37))
            learner.start(state(), attempt_id="before-restart")
            db.close()
            db = sqlite3.connect(path)
            learner = CombatLearner(db)
            self.assertIsNone(learner.pending)
            self.assertFalse(learner.should_close(0, "sword", 37))
            self.assertFalse(learner.start(state(), attempt_id="before-restart"))
            self.assertEqual(learner.summary(0, "sword")[0]["hits"], 5)
            db.close()

    def test_migration_preserves_existing_history_and_statistics(self):
        self.connection.execute("CREATE TABLE observations(id INTEGER PRIMARY KEY, state TEXT)")
        self.connection.execute("INSERT INTO observations VALUES(1,'keep')")
        self.connection.execute("PRAGMA user_version=7")
        self.connection.commit()
        CombatLearner(self.connection)
        self.assertEqual(self.connection.execute("SELECT state FROM observations").fetchone(), ("keep",))
        self.assertEqual(self.connection.execute("PRAGMA user_version").fetchone(), (7,))
        self.assertEqual(self.connection.execute("SELECT version FROM combat_learning_schema").fetchall(), [(1,)])

    def test_migration_rejects_unknown_version(self):
        self.connection.execute("UPDATE combat_learning_schema SET version=99")
        self.connection.commit()
        with self.assertRaisesRegex(ValueError, "schema"):
            CombatLearner(self.connection)
        self.assertEqual(self.connection.execute("SELECT version FROM combat_learning_schema").fetchone(), (99,))

    def test_nonfinite_planner_inputs_do_not_crash(self):
        for distance in (float("nan"), float("inf"), -1, None):
            self.assertFalse(self.learner.should_close(0, "sword", distance))

    def test_observed_results_change_future_range_preferences_without_qwen(self):
        for distance, damage, attempts in ((20, 8, 5), (37, 0, 4)):
            for _ in range(attempts):
                self.learner.start(state(distance=distance))
                self.now += .9
                self.learner.observe(state(distance=distance, enemy_hp=40-damage, recharge=50))
        self.assertTrue(self.learner.should_close(0, "sword", 37))
        self.assertFalse(self.learner.should_close(0, "sword", 20))
        self.assertFalse(self.learner.should_close(1, "sword", 37))

    def test_stats_and_evidence_roll_back_together(self):
        self.learner.start(state())
        self.connection.execute(
            "CREATE TRIGGER refuse_outcome BEFORE UPDATE ON combat_learning_attempts "
            "BEGIN SELECT RAISE(ABORT,'write failed'); END")
        self.now = .3
        with self.assertRaises(sqlite3.IntegrityError):
            self.learner.observe(state(enemy_hp=30, recharge=50))
        self.assertEqual(self.learner.summary(0, "sword"), [])
        self.assertEqual(self.connection.execute(
            "SELECT result FROM combat_learning_attempts").fetchone(), ("pending",))

    def test_planner_only_spends_practice_opportunities_on_ready_normal_attacks(self):
        from som_planner import MemoryPlanner
        from test_som_combat import fight
        planner = MemoryPlanner()
        planner.combat_learner = Mock()
        planner.combat_learner.should_close.return_value = False
        for recharge, level, enemy_hp, expected_calls in ((50, 0, 20, 0),
                                                         (0, 1, 100, 0),
                                                         (0, 0, 20, 1)):
            observed = fight(level=level, enemy_hp=enemy_hp)
            observed["party"][0]["weapon_recharge"] = recharge
            planner._combat(observed, observed["tile_context"], ["step_right"])
            self.assertEqual(planner.combat_learner.should_close.call_count, expected_calls)

    def test_dry_run_does_not_train_from_an_offline_wram_dump(self):
        from som_qwen_bridge import main
        with tempfile.TemporaryDirectory() as directory:
            raw = Path(directory) / "wram.bin"
            raw.write_bytes(bytes(0x20000))
            database = Path(directory) / "history.sqlite"
            with patch("sys.argv", ["som_qwen_bridge", "--policy", "memory", "--steps", "1",
                                    "--dry-run", "--wram-file", str(raw),
                                    "--history-db", str(database)]):
                self.assertEqual(main(), 0)
            db = sqlite3.connect(database)
            self.assertEqual(db.execute(
                "SELECT name FROM sqlite_master WHERE name='combat_learning_attempts'").fetchall(), [])
            db.close()


if __name__ == "__main__":
    unittest.main()

"""Fighting two enemies, facing tolerance and turn taps (user reports)."""
import unittest

from som_planner import MemoryPlanner


def state(enemies, rows=None, facing=None, hp=80):
    rows = rows or [".......", ".....", "..@..", ".....", "....."]
    return {"weapons_enabled": True,
            "party": [{"weapon_recharge": 0, "hp": hp, "max_hp": 100}],
            "enemy_slots": [dict(slot=i + 1, active=True, identity=5, x_px=88 + dx,
                                 y_px=88 + dy) for i, (dx, dy) in enumerate(enemies)],
            "tile_context": {"runtime_verified": True, "game_mode": "field", "radius": 2,
                             "map_id": 129, "map_size_tiles": [16, 16],
                             "actor_tile": [5, 5],
                             "actor": {"x_px": 88, "y_px": 88, "layer": 1,
                                       "facing": facing},
                             "passability_rows": list(rows)}}


class TestSplitFights(unittest.TestCase):
    def test_steps_out_from_between_two_enemies(self):
        decision = MemoryPlanner().decide(state([(24, 0), (-24, 0)]))
        self.assertIn(decision.action, ("step_up", "step_down"))
        self.assertIn("opposite sides", decision.reason)

    def test_no_split_move_without_a_helpful_open_step(self):
        corridor = [".....", "#####", "..@..", "#####", "....."]
        decision = MemoryPlanner().decide(state([(24, 0), (-24, 0)], corridor))
        self.assertNotIn("opposite sides", decision.reason)
        self.assertEqual(decision.intention, "engage_enemy")

    def test_enemy_far_behind_a_wall_is_not_a_split(self):
        # Live sample 23145: enemy 2 on top of the hero, enemy 1 76 px below.
        decision = MemoryPlanner().decide(state([(4, 76), (6, -8)]))
        self.assertNotIn("opposite sides", decision.reason)

    def test_critical_hp_does_not_reposition(self):
        decision = MemoryPlanner().decide(state([(24, 0), (-24, 0)], hp=20))
        self.assertNotIn("opposite sides", decision.reason)


class TestFacingAndTurns(unittest.TestCase):
    def test_turn_taps_toward_an_adjacent_enemy_are_never_stall_blocked(self):
        planner = MemoryPlanner()
        planner.facing_checks = [5, 0]
        observed = state([(14, 0)], facing="step_left")     # E010 never updates
        for _ in range(12):
            decision = planner.decide(observed, previous="step_right")
        self.assertEqual((decision.action, decision.intention),
                         ("step_right", "engage_enemy"))
        self.assertTrue(decision.turn_only)
        self.assertEqual(planner.blocked_moves, {})

    def test_enemy_near_the_diagonal_counts_as_in_front(self):
        planner = MemoryPlanner()
        planner.facing_checks = [5, 0]
        decision = planner.decide(state([(20, -18)], facing="step_right"))
        self.assertEqual(decision.action, "attack")
        decision = planner.decide(state([(10, -30)], facing="step_right"))
        self.assertEqual(decision.action, "step_up")

    def test_unverified_facing_keeps_the_last_turn_for_repeated_swings(self):
        planner = MemoryPlanner()
        observed = state([(16, 0)])
        actions = [planner.decide(observed, previous=previous).action
                   for previous in ("none", "step_right", "attack", "attack")]
        self.assertEqual(actions, ["step_right", "attack", "attack", "attack"])


if __name__ == "__main__":
    unittest.main()

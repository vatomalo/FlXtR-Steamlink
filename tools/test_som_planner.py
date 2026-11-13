"""Persistent planner tests; no emulator, model or network required."""
import tempfile
import unittest
from pathlib import Path

from som_memory import ObservationHistory
from som_planner import MemoryPlanner


def observation(rows=("...", ".@.", "..."), enemy=None, recharge=0, hp=80, max_hp=100):
    return {
        "weapons_enabled": True,
        "party": [{"weapon_recharge": recharge, "hp": hp, "max_hp": max_hp}],
        "enemy_slots": [enemy] if enemy else [],
        "tile_context": {
            "runtime_verified": True, "game_mode": "field", "radius": 1,
            "map_id": 131, "map_size_tiles": [8, 8], "actor_tile": [2, 3],
            "actor": {"x_px": 40, "y_px": 56, "layer": 1},
            "passability_rows": list(rows),
        },
    }


class TestMemoryPlanner(unittest.TestCase):
    def test_attacks_verified_nearby_enemy_when_weapon_ready(self):
        enemy = {"slot": 1, "active": True, "x_px": 64, "y_px": 56}
        decision = MemoryPlanner().decide(observation(enemy=enemy))
        self.assertEqual(decision.action, "attack")
        self.assertIn("enemy 1", decision.reason)

    def test_approaches_enemy_but_never_crosses_blocked_tile(self):
        enemy = {"slot": 1, "active": True, "x_px": 120, "y_px": 56}
        state = observation(("...", ".@#", "..."), enemy, recharge=20)
        self.assertNotEqual(MemoryPlanner().decide(state).action, "step_right")

    def test_persistent_frontier_intention_survives_decisions(self):
        with tempfile.TemporaryDirectory() as directory:
            history = ObservationHistory(Path(directory) / "memory.sqlite")
            planner = MemoryPlanner(history)
            state = observation()
            first = planner.decide(state)
            intention = history.get_agent_state("intention")
            second = planner.decide(state)
            self.assertEqual(first.intention, "explore_frontier")
            self.assertEqual(second.action, first.action)
            self.assertEqual(history.get_agent_state("intention")["target"],
                             intention["target"])
            self.assertEqual(len(history.known_cells(131, 1)), 9)
            history.close()

    def test_low_hp_intention_retreats_from_enemy(self):
        enemy = {"slot": 1, "active": True, "x_px": 64, "y_px": 56}
        decision = MemoryPlanner().decide(
            observation(enemy=enemy, recharge=20, hp=20, max_hp=100))
        self.assertEqual(decision.intention, "survive")
        self.assertNotEqual(decision.action, "step_right")

    def test_combat_intention_survives_transient_missing_enemy(self):
        enemy = {"slot": 1, "active": True, "x_px": 120, "y_px": 56}
        planner = MemoryPlanner()
        first = planner.decide(observation(enemy=enemy, recharge=20))
        missing = planner.decide(observation(enemy=None, recharge=20), first.action)
        self.assertEqual(first.intention, "engage_enemy")
        self.assertEqual(missing.intention, "engage_enemy")
        self.assertIn("last-known", missing.reason)

    def test_qwen_heading_directs_frontier_target(self):
        east = MemoryPlanner().decide(observation(), directive="explore_east")
        west = MemoryPlanner().decide(observation(), directive="explore_west")
        self.assertEqual(east.action, "step_right")
        self.assertEqual(west.action, "step_left")

    def test_fails_closed_without_verified_context(self):
        state = observation()
        state["tile_context"]["runtime_verified"] = False
        self.assertEqual(MemoryPlanner().decide(state).action, "wait")


if __name__ == "__main__":
    unittest.main()

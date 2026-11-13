"""Persistent planner tests; no emulator, model or network required."""
import tempfile
import unittest
from pathlib import Path

from som_memory import ObservationHistory
from som_planner import MemoryPlanner


def observation(rows=("...", ".@.", "..."), enemy=None, recharge=0):
    return {
        "weapons_enabled": True,
        "party": [{"weapon_recharge": recharge}],
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

    def test_persistent_visits_change_exploration_choice(self):
        with tempfile.TemporaryDirectory() as directory:
            history = ObservationHistory(Path(directory) / "memory.sqlite")
            planner = MemoryPlanner(history)
            state = observation()
            self.assertEqual(planner.decide(state).action, "step_right")
            # Persist a visit to the right-hand tile as if the actor arrived.
            moved = observation()
            moved["tile_context"]["actor_tile"] = [3, 3]
            moved["tile_context"]["actor"]["x_px"] = 56
            history.remember_position(moved)
            history.remember_position(state)
            self.assertEqual(planner.decide(state).action, "step_down")
            history.close()

    def test_fails_closed_without_verified_context(self):
        state = observation()
        state["tile_context"]["runtime_verified"] = False
        self.assertEqual(MemoryPlanner().decide(state).action, "wait")


if __name__ == "__main__":
    unittest.main()

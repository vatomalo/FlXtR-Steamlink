"""Tile-safety tests; no Ollama, SSH, ROM, or emulator required."""
import unittest

from som_qwen_bridge import guard_tile_action


class TestTileGuard(unittest.TestCase):
    def observation(self, rows, verified=True):
        return {"tile_context": {
            "runtime_verified": verified,
            "game_mode": "field",
            "radius": 1,
            "passability_rows": rows,
        }}

    def test_allows_only_verified_open_step(self):
        state = self.observation(["...", ".@.", "..."])
        self.assertEqual(guard_tile_action("step_right", state), "step_right")
        self.assertEqual(guard_tile_action("attack", state), "attack")

    def test_blocks_solid_and_conditional_tiles(self):
        self.assertEqual(
            guard_tile_action("step_right", self.observation(["...", ".@#", "..."])),
            "wait")
        self.assertEqual(
            guard_tile_action("step_up", self.observation([".?.", ".@.", "..."])),
            "wait")

    def test_fails_closed_without_verified_grid(self):
        self.assertEqual(guard_tile_action("step_down", None), "wait")
        self.assertEqual(
            guard_tile_action("step_down", self.observation(["...", ".@.", "..."], False)),
            "wait")

    def test_field_actions_wait_during_menu_or_transition(self):
        state = self.observation(["...", ".@.", "..."])
        state["tile_context"]["game_mode"] = "ring"
        self.assertEqual(guard_tile_action("attack", state), "wait")
        self.assertEqual(guard_tile_action("step_left", state), "wait")


if __name__ == "__main__":
    unittest.main()

"""Tile-safety tests; no Ollama, SSH, ROM, or emulator required."""
import unittest

from som_qwen_bridge import guard_tile_action, parse_action, prefer_open_step


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

    def test_wait_falls_back_to_verified_open_tile(self):
        state = self.observation([".#.", "#@.", ".#."])
        self.assertEqual(prefer_open_step("wait", state, "none"), "step_right")
        self.assertEqual(prefer_open_step("wait", state, "step_right"), "step_right")

    def test_wait_stays_when_no_open_tile_exists(self):
        state = self.observation(["###", "#@#", "###"])
        self.assertEqual(prefer_open_step("wait", state, "none"), "wait")


class TestParseAction(unittest.TestCase):
    def test_accepts_common_small_model_formats(self):
        self.assertEqual(parse_action("step_right"), "step_right")
        self.assertEqual(parse_action("  Step_Right.\n"), "step_right")
        self.assertEqual(parse_action("`attack`"), "attack")
        self.assertEqual(parse_action("Action: step-down"), "step_down")
        self.assertEqual(parse_action("<think>\n\n</think>\n\nwait"), "wait")

    def test_ignores_actions_mentioned_only_while_thinking(self):
        self.assertIsNone(parse_action("<think>maybe attack or step_up"))
        self.assertEqual(parse_action("<think>attack?</think> step_up"), "step_up")

    def test_rejects_unknown_text(self):
        self.assertIsNone(parse_action(""))
        self.assertIsNone(parse_action("jump"))
        self.assertIsNone(parse_action("rm -rf /"))
        self.assertIsNone(parse_action("upstairs"))


if __name__ == "__main__":
    unittest.main()

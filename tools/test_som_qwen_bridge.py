"""Tile-safety tests; no Ollama, SSH, ROM, or emulator required."""
import os
import stat
import tempfile
import unittest
from pathlib import Path

from som_qwen_bridge import (REMOTE_LOOP, RemoteLink, 
    allowed_directives, build_director_prompt, guard_tile_action, parse_action,
    parse_directive, prefer_open_step, extend_clear_exploration,
    objective_directive, shop_errand)


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

    def test_trusted_steps_pass_the_guard(self):
        state = self.observation(["...", "?@.", "..."], verified=False)
        self.assertEqual(guard_tile_action("step_left", state), "wait")
        self.assertEqual(guard_tile_action("step_left", state, {"step_left"}), "step_left")
        self.assertEqual(guard_tile_action("step_up", state, {"step_left"}), "wait")

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

    def test_extends_only_fully_clear_exploration_routes(self):
        clear = {"tile_context": {"runtime_verified": True, "game_mode": "field",
                                  "radius": 3, "passability_rows": ["......."] * 7}}
        self.assertEqual(
            extend_clear_exploration("step_right", clear, "explore_frontier", 8), "right")
        # Holds are preemptible, so every movement intention may use them.
        self.assertEqual(
            extend_clear_exploration("step_right", clear, "engage_enemy", None), "right")
        self.assertEqual(
            extend_clear_exploration("step_right", clear, "observe", None), "step_right")
        # Brake with short pulses for the last two tiles before a target.
        self.assertEqual(
            extend_clear_exploration("step_right", clear, "explore_frontier", 2),
            "step_right")
        blocked = {"tile_context": dict(clear["tile_context"])}
        blocked["tile_context"]["passability_rows"] = [".......", ".......", ".......",
                                                          "...@.#.", ".......", ".......",
                                                          "......."]
        self.assertEqual(
            extend_clear_exploration("step_right", blocked, "explore_frontier", 8),
            "step_right")


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


class TestQwenDirector(unittest.TestCase):
    def test_explicit_mission_heading_is_immediate(self):
        self.assertEqual(objective_directive("Go to the woods WEST and level up"),
                         "explore_west")
        self.assertEqual(objective_directive("Explore safely"), "explore_unknown")

    def test_parses_only_allowlisted_intentions(self):
        self.assertEqual(parse_directive("explore east"), "explore_east")
        self.assertEqual(parse_directive("Intent: engage_enemy."), "engage_enemy")
        self.assertIsNone(parse_directive("spawn_everything"))

    def test_combat_intentions_require_a_verified_enemy(self):
        state = {"party": [{"hp": 80, "max_hp": 100}], "enemy_slots": []}
        self.assertNotIn("engage_enemy", allowed_directives(state))
        state["enemy_slots"] = [{"active": True}]
        self.assertEqual(allowed_directives(state)[0], "engage_enemy")
        state["party"][0]["hp"] = 20
        self.assertEqual(allowed_directives(state)[0], "avoid_enemy")

    def test_prompt_exposes_location_memory_and_choices(self):
        state = {"party": [{"hp": 68, "max_hp": 98}], "enemy_slots": [],
                 "tile_context": {"map_id": 131, "actor_tile": [10, 23],
                                  "actor": {"layer": 1}}}
        prompt = build_director_prompt("Find progress", state, "explore_unknown",
                                       ["step_right"], 42)
        self.assertIn("Potos Village", prompt)
        self.assertIn("Remembered places", prompt)
        self.assertIn("known map cells 42", prompt)
        self.assertIn("explore_east", prompt)
        learned = [{"action": "attack", "damage_dealt": 12}]
        prompt = build_director_prompt("Train", state, "explore_unknown", [], 42,
                                       learned)
        self.assertIn("damage_dealt", prompt)



class TestShopErrand(unittest.TestCase):
    def setUp(self):
        from som_memory import WRAM
        self.ram = bytearray(0x20000)
        self.ram[0xCC48] = 0x22            # one Candy
        self.ram[0xCC49:0xCC53] = b"\xff" * 10
        self.ram[0xCC6A:0xCC6D] = (800).to_bytes(3, "little")
        self.wram = lambda: WRAM(bytes(self.ram))
        self.shop = {"map_id": 131, "layer": 1, "target": [1, 1], "price": 10}
        self.gold = {"address": 0x7ECC6A, "width": 3}

    def state(self, hp):
        return {"party": [{"hp": hp, "max_hp": 100}]}

    def test_shops_when_healthy_and_rich(self):
        errand = shop_errand(self.wram(), self.state(90), self.shop, self.gold, 500)
        self.assertEqual(errand["place"], "shop")

    def test_no_shopping_when_poor_hurt_full_or_untaught(self):
        self.assertIsNone(shop_errand(self.wram(), self.state(90), self.shop, self.gold, 900))
        self.assertIsNone(shop_errand(self.wram(), self.state(40), self.shop, self.gold, 500))
        self.assertIsNone(shop_errand(self.wram(), self.state(90), None, self.gold, 500))
        self.ram[0xCC48] = 0x80             # four Candy
        self.assertIsNone(shop_errand(self.wram(), self.state(90), self.shop, self.gold, 500))

class TestRemoteLink(unittest.TestCase):
    """Run the persistent protocol through a fake ssh that executes locally."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        root = Path(self.tmp.name)
        self.root = root
        fake = root / "fake-ssh"
        # Ignore ssh options and host; run the remote loop with local sh.
        fake.write_text('#!/bin/sh\nfor last; do :; done\nexec sh -c "$last"\n')
        fake.chmod(fake.stat().st_mode | stat.S_IEXEC)
        loop = REMOTE_LOOP.replace("/tmp/", str(root) + "/")
        self.link = RemoteLink("host", timeout=5, remote_loop=loop, ssh_command=[str(fake)])

    def tearDown(self):
        self.link.close()
        self.tmp.cleanup()

    def test_reads_wram_and_sends_actions_over_one_session(self):
        image = bytes(range(256)) * 512
        (self.root / "flxtr-som-wram.bin").write_bytes(image)
        (self.root / "flxtr-som-ai-enabled").write_text("enabled\n")
        self.assertEqual(self.link.fetch_wram(require_marker=True), image)
        process = self.link.process
        self.link.send_action("up")
        self.assertEqual((self.root / "flxtr-som-ai-action").read_text(), "up\n")
        self.assertEqual(self.link.fetch_wram(), image)
        self.assertIs(self.link.process, process)

    def test_missing_marker_means_takeover(self):
        (self.root / "flxtr-som-wram.bin").write_bytes(bytes(0x20000))
        self.assertIsNone(self.link.fetch_wram(require_marker=True))

    def test_refuses_non_allowlisted_action_locally(self):
        with self.assertRaises(ValueError):
            self.link.send_action("$(reboot)")
        self.assertFalse((self.root / "flxtr-som-ai-action").exists())

if __name__ == "__main__":
    unittest.main()

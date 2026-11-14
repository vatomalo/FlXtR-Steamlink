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

    def test_dashes_on_long_straight_routes_away_from_monsters(self):
        def wide(enemies=()):
            return {"tile_context": {"runtime_verified": True, "game_mode": "field", "radius": 5,
                                     "actor": {"x_px": 400, "y_px": 400},
                                     "passability_rows": ["..........."] * 11},
                    "enemy_slots": list(enemies)}
        self.assertEqual(extend_clear_exploration("step_right", wide(), "explore_frontier", 10, 10),
                         "dash_right")
        self.assertEqual(extend_clear_exploration("step_up", wide(), "go_to_inn", None, 12),
                         "dash_up")
        # Arguments are (remaining tiles, straight tiles).
        self.assertEqual(extend_clear_exploration("step_right", wide(), "explore_frontier", 10, 6),
                         "dash_right")                                  # 6-tile straight run
        # Short runs, near targets, nearby monsters and fights keep the walk.
        self.assertEqual(extend_clear_exploration("step_right", wide(), "explore_frontier", 10, 5),
                         "right")                                       # straight run too short
        self.assertEqual(extend_clear_exploration("step_right", wide(), "explore_frontier", 7, 10),
                         "right")                                       # target too near
        near = {"active": True, "x_px": 500, "y_px": 420}
        self.assertEqual(extend_clear_exploration("step_right", wide([near]), "explore_frontier",
                                                  10, 10), "right")
        self.assertEqual(extend_clear_exploration("step_right", wide(), "engage_enemy", 10, 10),
                         "right")

    def test_extends_only_fully_clear_exploration_routes(self):
        clear = {"tile_context": {"runtime_verified": True, "game_mode": "field",
                                  "radius": 3, "passability_rows": ["......."] * 7}}
        self.assertEqual(
            extend_clear_exploration("step_right", clear, "explore_frontier", 8, 8), "step_right")
        wide = {"tile_context": {"runtime_verified": True, "game_mode": "field",
                                 "radius":5, "passability_rows":["..........."]*11}}
        self.assertEqual(extend_clear_exploration("step_right", wide, "explore_frontier", 8,8), "right")
        self.assertEqual(extend_clear_exploration("step_right", wide, "explore_frontier", 4,8), "step_right")
        river = {"tile_context": {"runtime_verified":True,"game_mode":"field","radius":5,
                    "actor":{"reported_ground_effect":0x31},
                    "passability_rows":["..........."]*11,
                    "attribute_rows":[" ".join(["02023100"]*11)]*11}}
        self.assertEqual(extend_clear_exploration("step_up",river,"go_to_destination",8,8),"up")
        self.assertEqual(extend_clear_exploration("step_down",river,"go_to_destination",8,8),"step_down")
        # Holds are preemptible, so every movement intention may use them.
        self.assertEqual(
            extend_clear_exploration("step_right", clear, "engage_enemy", None, 5), "step_right")
        self.assertEqual(
            extend_clear_exploration("step_right", clear, "observe", None), "step_right")
        # The route turns after two tiles: a hold would overshoot the corner.
        self.assertEqual(
            extend_clear_exploration("step_right", clear, "explore_frontier", 40, 2),
            "step_right")
        self.assertEqual(
            extend_clear_exploration("step_right", clear, "explore_frontier", 40, None),
            "step_right")
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

class TestEagleEye(unittest.TestCase):
    def field_wram(self):
        ram = bytearray(0x20000)
        ram[0xE000] = 1                      # hero present
        ram[0x00E4], ram[0x00E6] = 1, 0x0F   # field mode
        ram[0x00C0:0x00C2] = (64 * 16).to_bytes(2, "little")   # 64 x 64 tile map
        ram[0x00C2:0x00C4] = (64 * 16).to_bytes(2, "little")
        ram[0xE002:0xE004] = (20 * 16 + 8).to_bytes(2, "little")
        ram[0xE004:0xE006] = (30 * 16 + 8).to_bytes(2, "little")
        ram[0x1B800 + 4 * 7] = 0x13          # $7F:B800 attributes: tile id 7 is solid
        for x in range(0, 64):
            ram[0x10000 + 10 * 128 + x] = 7  # a wall along row 10, far outside 7x7
        return bytes(ram)

    def test_wide_scan_maps_far_tiles_quickly(self):
        import time as clock
        from som_memory import ObservationHistory
        from som_qwen_bridge import survey
        with tempfile.TemporaryDirectory() as directory:
            history = ObservationHistory(Path(directory) / "eagle.sqlite")
            started = clock.monotonic()
            learned = survey(history, self.field_wram(), 32)
            elapsed = clock.monotonic() - started
            self.assertEqual(learned, 65 * 65)
            cells = history.known_cells(0, 1)
            self.assertEqual(cells[(20, 10)], "blocked")   # 20 tiles north of the hero
            self.assertEqual(cells[(20, 29)], "open")
            self.assertLess(elapsed, 2.0)
            history.close()

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

    def test_compressed_and_plain_reads_return_identical_wram(self):
        image = bytes((i * 7) & 0xFF if i % 3 else 0 for i in range(0x20000))
        (self.root / "flxtr-som-wram.bin").write_bytes(image)
        self.assertTrue(self.link.compress)
        self.assertEqual(self.link.fetch_wram(), image)
        self.link.compress = False
        self.assertEqual(self.link.fetch_wram(), image)
        self.link.compress = True
        self.assertEqual(self.link.fetch_wram(), image)     # stream stays in sync

    def test_hot_reads_patch_changes_and_refresh_cold_areas(self):
        from som_qwen_bridge import FULL_EVERY
        path = self.root / "flxtr-som-wram.bin"
        image = bytearray(0x20000)
        path.write_bytes(image)
        self.assertEqual(self.link.fetch_wram(), bytes(image))          # full: base
        image[0xE002] = 77                                                 # hero x (hot)
        cold = 0x14010                                                     # 7F:4010 (cold)
        image[cold] = 9
        path.write_bytes(image)
        hot = self.link.fetch_wram()
        self.assertEqual(hot[0xE002], 77)
        self.assertEqual(hot[cold], 0)                                     # not yet
        for _ in range(FULL_EVERY):
            refreshed = self.link.fetch_wram()
        self.assertEqual(refreshed[cold], 9)                               # full refresh
        image[0xDC] = 131                                                  # map change
        image[cold] = 3
        path.write_bytes(image)
        self.assertEqual(self.link.fetch_wram()[cold], 3)                  # full at once

    def test_missing_snapshot_answers_none_and_keeps_the_session(self):
        for compress in (True, False):
            self.link.compress = compress
            with self.assertRaises(OSError) as raised:
                self.link.fetch_wram()
            self.assertIn("NONE", str(raised.exception))
        process = self.link.process
        (self.root / "flxtr-som-wram.bin").write_bytes(bytes(0x20000))
        self.link.compress = True
        self.assertEqual(self.link.fetch_wram(), bytes(0x20000))
        self.assertIs(self.link.process, process)              # same shell survived

    def test_reads_the_gambit_mask_the_player_set(self):
        import som_qwen_bridge as bridge
        path = Path(bridge.GAMBIT_FILE)
        loop = self.link.remote_loop.replace(str(path), str(self.root / "gambits"))
        self.link.close()
        self.link.remote_loop = loop
        defaults = tuple(int(c) for c in bridge.GAMBIT_DEFAULTS)
        self.assertEqual(self.link.fetch_gambits(), defaults)              # no file: defaults
        (self.root / "gambits").write_text("504321\n")
        self.assertEqual(self.link.fetch_gambits(), (5, 0, 4, 3, 2, 1))
        for bad in ("rm -rf /\n", "9999999\n", "12\n"):
            (self.root / "gambits").write_text(bad)
            self.assertEqual(self.link.fetch_gambits(), defaults)          # garbage ignored

    def test_gambit_weights_are_weighed_against_each_other(self):
        from types import SimpleNamespace
        import som_qwen_bridge as bridge
        planner, loop = SimpleNamespace(), SimpleNamespace()
        bridge.apply_gambits(planner, loop, (5, 1, 3, 3, 4, 1))     # fight >> inn
        self.assertLess(planner.retreat_fraction, 0.5)
        self.assertLess(loop.leave_fraction, 0.6)
        bridge.apply_gambits(planner, loop, (1, 5, 3, 3, 4, 1))     # inn >> fight
        self.assertGreater(planner.retreat_fraction, 0.5)
        bridge.apply_gambits(planner, loop, (0, 0, 3, 3, 4, 1))
        self.assertTrue(planner.peaceful and planner.no_retreat)
        self.assertEqual(bridge.candy_fraction((4, 3, 3, 3, 4, 1)), 0.5)   # candy >= inn
        self.assertEqual(bridge.candy_fraction((4, 5, 3, 3, 2, 1)), 0.25)  # inn first

    def test_familiar_slot_is_read_and_hidden_from_the_enemies(self):
        import som_qwen_bridge as bridge
        fam = self.root / "flxtr-som-familiar"          # setUp maps /tmp/ to root
        self.assertEqual(self.link.fetch_familiar(), 0)
        fam.write_text("2\n")
        self.assertEqual(self.link.fetch_familiar(), 2)
        fam.write_text("; rm -rf /\n")
        self.assertEqual(self.link.fetch_familiar(), 0)
        observation = {"enemy_slots": [{"slot": 1, "active": True}, {"slot": 2, "active": True}]}
        bridge.hide_familiar(observation, 2)
        self.assertEqual([e["active"] for e in observation["enemy_slots"]], [True, False])

    def test_direct_candy_needs_two_and_a_confirmed_heal(self):
        import som_qwen_bridge as bridge
        def image(candy_code, hp):
            ram = bytearray(0x20000)
            ram[0xCC48:0xCC48 + 11] = b"\xff" * 11
            ram[0xCC48] = candy_code << 4
            ram[0xE000] = 1
            ram[0xE182], ram[0xE184] = hp, 117
            return bytes(ram)
        class Link:
            def __init__(self, after): self.after, self.sent = after, []
            def send_control(self, command): self.sent.append(command)
            def fetch_wram(self, marker=False): return self.after
        healed = Link(image(4, 117))
        self.assertTrue(bridge.direct_candy(healed, image(6, 40)))
        self.assertEqual(healed.sent, ["use_candy"])
        last = Link(image(0, 117))
        self.assertFalse(bridge.direct_candy(last, image(2, 40)))         # 1 left: ring
        self.assertEqual(last.sent, [])
        nothing = Link(image(6, 40))
        self.assertFalse(bridge.direct_candy(nothing, image(6, 40), timeout=0.3))  # not confirmed

    def test_missing_marker_means_takeover(self):
        (self.root / "flxtr-som-wram.bin").write_bytes(bytes(0x20000))
        self.assertIsNone(self.link.fetch_wram(require_marker=True))

    def test_speech_bubble_prefix_survives_the_remote_protocol(self):
        self.link.show_message("^Hello there!|Second line")
        self.assertEqual((self.root / "flxtr-som-ai-message").read_text(),
                         "^Hello there!|Second line\n")

    def test_navigation_reads_are_small_coherent_and_takeover_guarded(self):
        raw=bytearray(0x20000)
        raw[0xE4],raw[0xE6],raw[0xD9],raw[0xF4]=1,15,1,42
        raw[0xDC:0xDE]=(128).to_bytes(2,"little")
        raw[0xE000],raw[0xE02C],raw[0xE048]=1,1,0x31
        raw[0xE002:0xE004]=(536).to_bytes(2,"little")
        raw[0xE004:0xE006]=(866).to_bytes(2,"little")
        raw[0xE182:0xE184]=(98).to_bytes(2,"little")
        raw[0xE184:0xE186]=(98).to_bytes(2,"little")
        (self.root/"flxtr-som-wram.bin").write_bytes(raw)
        self.assertIsNone(self.link.fetch_navigation())
        (self.root/"flxtr-som-ai-enabled").write_text("enabled\n")
        frame=self.link.fetch_navigation()
        self.assertTrue(frame["valid"])
        self.assertEqual((frame["tick"],frame["x"],frame["y"]),(42,536,866))

    def test_control_commands_are_separate_from_model_actions(self):
        with self.assertRaises(ValueError):
            self.link.send_action("clone_enemy")
        self.link.send_control("clone_enemy")
        self.assertEqual((self.root / "flxtr-som-ai-action").read_text(), "clone_enemy\n")
        with self.assertRaises(ValueError):
            self.link.send_control("rm")

    def test_refuses_non_allowlisted_action_locally(self):
        with self.assertRaises(ValueError):
            self.link.send_action("$(reboot)")
        self.assertFalse((self.root / "flxtr-som-ai-action").exists())


class LoopMessageTest(unittest.TestCase):
    def test_loop_messages(self):
        from som_qwen_bridge import MESSAGE_ALLOWED as allowed, loop_message
        for goal in ("hunt_map_129", "hunt_here", "go_to_shop", "go_to_inn", "go_to_save"):
            text = loop_message(goal)
            self.assertTrue(text.startswith("AI: "))
            self.assertIsNone(allowed.search(text), text)
        self.assertIn("129", loop_message("hunt_map_129"))

if __name__ == "__main__":
    unittest.main()

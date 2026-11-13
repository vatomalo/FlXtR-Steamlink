"""Persistent planner tests; no emulator, model or network required."""
import tempfile
import unittest
from pathlib import Path

from som_memory import ObservationHistory
from som_planner import MemoryPlanner, PlannerDecision


def PlannerDecisionStub(action):
    return PlannerDecision(action, "test", "explore_frontier")


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
        # Facing byte not yet verified: turn toward the enemy, then swing.
        enemy = {"slot": 1, "active": True, "x_px": 64, "y_px": 56}
        planner = MemoryPlanner()
        self.assertEqual(planner.decide(observation(enemy=enemy)).action, "step_right")
        decision = planner.decide(observation(enemy=enemy), previous="step_right")
        self.assertEqual(decision.action, "attack")
        self.assertIn("enemy 1", decision.reason)

    def test_does_not_swing_at_enemy_outside_contact_reach(self):
        planner = MemoryPlanner()
        planner.assumed_facing = "step_right"
        state = observation(enemy={"slot": 1, "active": True,
                                   "x_px": 80, "y_px": 56}, recharge=0)
        decision = planner.decide(state, previous="step_right")
        self.assertNotIn(decision.action, ("attack", "charge"),
                         decision.reason)

    def test_contact_range_allows_verified_facing_attack(self):
        planner = MemoryPlanner()
        planner.assumed_facing = "step_right"
        state = observation(enemy={"slot": 1, "active": True,
                                   "x_px": 64, "y_px": 56}, recharge=0)
        decision = planner.decide(state, previous="step_right")
        self.assertEqual(decision.action, "attack", decision.reason)

    def test_swings_only_while_facing_the_enemy(self):
        planner = MemoryPlanner()
        planner.facing_checks = [5, 0]                 # E010 verified
        enemy = {"slot": 1, "active": True, "x_px": 40, "y_px": 32}   # 24px straight above, within melee reach
        state = observation(enemy=enemy)
        state["tile_context"]["actor"]["facing"] = "step_right"
        self.assertEqual(planner.decide(state).action, "step_up")
        state["tile_context"]["actor"]["facing"] = "step_up"
        self.assertEqual(planner.decide(state).action, "attack")
        # Wall above: turn along the other axis only if the enemy is off it.
        state["tile_context"]["actor"]["facing"] = "step_right"
        walled = observation(("###", ".@.", "..."), enemy=dict(enemy, x_px=52))
        walled["tile_context"]["actor"]["facing"] = "step_down"
        self.assertEqual(planner.decide(walled).action, "step_right")

    def test_facing_byte_is_trusted_only_after_real_steps_agree(self):
        planner = MemoryPlanner()
        for x in range(40, 52, 2):
            state = observation()
            state["tile_context"]["actor"].update(x_px=x, facing="step_right")
            planner.decide(state, previous="step_right")
        self.assertTrue(planner.facing_trusted())
        wrong = MemoryPlanner()
        for x in range(40, 52, 2):
            state = observation()
            state["tile_context"]["actor"].update(x_px=x, facing="step_up")
            wrong.decide(state, previous="step_right")
        self.assertFalse(wrong.facing_trusted())

    def test_approaches_enemy_but_never_crosses_blocked_tile(self):
        enemy = {"slot": 1, "active": True, "x_px": 120, "y_px": 56}
        state = observation(("...", ".@#", "..."), enemy, recharge=20)
        self.assertNotEqual(MemoryPlanner().decide(state).action, "step_right")

    def test_walled_enemy_routes_around_or_is_ignored_instead_of_flip_flopping(self):
        # Enemy straight above, wall directly above, open sides.
        enemy = {"slot": 2, "active": True, "x_px": 40, "y_px": 0}
        planner = MemoryPlanner()
        state = observation(("###", ".@.", "..."), enemy, recharge=20)
        first = planner.decide(state)
        self.assertEqual(first.intention, "engage_enemy")
        self.assertNotEqual(first.action, "step_up")
        if first.action == "wait":
            self.assertIn("unreachable", first.reason)
            # Ignored enemy no longer pulls the hero back every other decision.
            again = planner.decide(state)
            self.assertNotEqual(again.intention, "engage_enemy")

    def test_unreachable_enemy_cooldown_expires(self):
        enemy = {"slot": 3, "active": True, "x_px": 40, "y_px": 0}
        planner = MemoryPlanner()
        boxed = observation(("###", "#@#", "###"), enemy, recharge=20)
        self.assertIn("unreachable", planner.decide(boxed).reason)
        later = [planner.decide(boxed) for _ in range(25)]
        ignored = [decision for decision in later if decision.intention != "engage_enemy"]
        self.assertGreaterEqual(len(ignored), 15)
        # After the cooldown the enemy is reconsidered (and re-ignored if still walled).
        self.assertTrue(any("unreachable" in decision.reason for decision in later))

    def test_evasion_keeps_heading_between_equal_escape_steps(self):
        # Low HP; enemy straight below so left and right are equally safe.
        enemy = {"slot": 1, "active": True, "x_px": 40, "y_px": 120}
        planner = MemoryPlanner()
        actions = [planner.decide(observation(("...", ".@.", "..."), enemy,
                                              hp=20, max_hp=100)).action
                   for _ in range(6)]
        for before, after in zip(actions, actions[1:]):
            self.assertNotEqual({before, after}, {"step_left", "step_right"}, actions)

    def test_observe_reports_verification_reason(self):
        state = observation()
        state["tile_context"].update(runtime_verified=False,
                                     verification_error="not_field_mode",
                                     game_mode="event")
        decision = MemoryPlanner().decide(state)
        self.assertEqual(decision.action, "wait")
        self.assertIn("not_field_mode", decision.reason)

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
        self.assertEqual(decision.intention, "return_to_inn")
        self.assertNotEqual(decision.action, "step_right")

    def test_half_health_latches_return_to_inn_until_recovered(self):
        planner = MemoryPlanner()
        hurt = planner.decide(observation(hp=50, max_hp=100))
        still_hurt = planner.decide(observation(hp=80, max_hp=100), hurt.action)
        recovered = planner.decide(observation(hp=90, max_hp=100), still_hurt.action)
        self.assertEqual(hurt.intention, "return_to_inn")
        self.assertEqual(still_hurt.intention, "return_to_inn")
        self.assertNotEqual(recovered.intention, "return_to_inn")

    def test_survival_gives_up_when_inn_spot_does_not_heal(self):
        with tempfile.TemporaryDirectory() as directory:
            history = ObservationHistory(Path(directory) / "inn.sqlite")
            history.set_agent_state("safe_anchor",
                                    {"map_id": 131, "layer": 1, "target": [2, 3]})
            planner = MemoryPlanner(history)
            hurt = observation(hp=40, max_hp=100)
            decisions = [planner.decide(hurt) for _ in range(40)]
            self.assertEqual(decisions[0].intention, "recover_at_inn")
            abandoned = [d for d in decisions if "survival abandoned" in d.reason]
            self.assertEqual(len(abandoned), 1)
            self.assertIn("innkeeper", abandoned[0].reason)
            self.assertNotIn(decisions[-1].intention, ("recover_at_inn", "return_to_inn"))
            # A new critical drop restarts the retreat.
            critical = planner.decide(observation(hp=20, max_hp=100))
            self.assertIn(critical.intention, ("recover_at_inn", "return_to_inn"))
            history.close()

    def test_errand_travels_to_taught_shop_and_reports_arrival(self):
        shop = {"map_id": 131, "layer": 1, "target": [1, 3], "face": "step_up"}
        planner = MemoryPlanner()
        going = planner.decide(observation(), errand={"place": "shop", "anchor": shop})
        self.assertEqual((going.action, going.intention), ("step_left", "go_to_shop"))
        here = dict(shop, target=[2, 3])
        arrived = planner.decide(observation(), errand={"place": "shop", "anchor": here})
        self.assertEqual((arrived.action, arrived.intention), ("wait", "at_shop"))

    def test_enemy_on_the_way_is_fought_before_shopping(self):
        shop = {"map_id": 131, "layer": 1, "target": [1, 3]}
        enemy = {"slot": 1, "active": True, "x_px": 64, "y_px": 56}
        decision = MemoryPlanner().decide(observation(enemy=enemy),
                                          errand={"place": "shop", "anchor": shop})
        self.assertEqual(decision.intention, "engage_enemy")

    def test_bridge_crossed_once_makes_identical_tiles_walkable_everywhere(self):
        bridge = {"classification": "conditional", "attributes": "08001000",
                  "reasons": ["layer_switch"]}
        with tempfile.TemporaryDirectory() as directory:
            history = ObservationHistory(Path(directory) / "bridge.sqlite")
            planner = MemoryPlanner(history)
            # Before: a conditional tile to the left is refused.
            ahead = observation(("...", "?@.", "..."))
            ahead["tile_context"]["special_cells"] = [dict(bridge, offset=[-1, 0])]
            self.assertNotIn("step_left", planner.trusted_steps(ahead))
            # The player stands on such a tile once (on another map).
            standing = observation(("...", ".@.", "..."))
            standing["tile_context"].update(map_id=200, special_cells=[
                dict(bridge, offset=[0, 0])])
            history.remember_walk(standing)
            self.assertIn("step_left", planner.trusted_steps(ahead))
            decision = planner.decide(ahead, directive="explore_west")
            self.assertNotEqual(decision.intention, "observe")
            history.close()

    def test_follows_taught_path_when_tile_check_fails_on_bridge(self):
        planner = MemoryPlanner()
        for x in (2, 1, 0):
            planner.local_walked.setdefault(131, set()).add((x, 3))
        state = observation()
        state["tile_context"].update(runtime_verified=False,
                                     verification_error="actor_tile_fields_disagree")
        decision = planner.decide(state, "step_left", "explore_west")
        self.assertEqual((decision.action, decision.intention),
                         ("step_left", "follow_taught_path"))
        state["tile_context"]["actor_tile"] = [5, 5]   # off the taught path
        self.assertEqual(planner.decide(state, "step_left").intention, "observe")

    def test_step_that_never_moves_the_hero_is_blocked_and_avoided(self):
        planner = MemoryPlanner()
        # Only "left" looks open, but the hero never actually moves.
        state = observation(("###", ".@#", "###"))
        decisions = [planner.decide(state, directive="explore_west") for _ in range(9)]
        self.assertEqual(decisions[0].action, "step_left")
        stalls = [d for d in decisions if "no progress" in d.reason]
        self.assertEqual(len(stalls), 1)
        self.assertNotEqual(decisions[-1].action, "step_left")
        self.assertNotIn("step_left", planner.trusted_steps(state))
        # Moving resets the count: no block while the position changes.
        moving = MemoryPlanner()
        for x in range(12):
            state = observation(("...", ".@.", "..."))
            state["tile_context"]["actor"]["x_px"] = 40 - x
            self.assertNotIn("no progress", moving.decide(state, "step_left",
                                                          "explore_west").reason)

    @staticmethod
    def grass_state(left_marker, left_tile="10", left_attrs="03004000", others="00000000"):
        state = observation(("###", left_marker + "@#", "###"))
        attrs = [" ".join([others] * 3),
                 " ".join([left_attrs, others, others]),
                 " ".join([others] * 3)]
        ids = ["01 01 01", f"{left_tile} 02 01", "01 01 01"]
        state["tile_context"].update(attribute_rows=attrs, tile_id_rows=ids)
        return state

    def test_learns_grass_from_a_cut_then_cuts_identical_grass(self):
        planner = MemoryPlanner()
        planner.decide(self.grass_state("#"))
        planner.decide(self.grass_state(".", left_tile="11", left_attrs="00000000"))
        self.assertIn("03004000", planner.local_cuttable)
        # Elsewhere: grass with the same attributes blocks the only way west.
        grass = self.grass_state("#")
        grass["tile_context"]["map_id"] = 140
        face = planner.decide(grass, "wait", "explore_west")
        self.assertEqual((face.action, face.intention), ("step_left", "cut_grass"))
        self.assertIn("step_left", planner.trusted_steps(grass))
        attack = planner.decide(grass, face.action, "explore_west")
        self.assertEqual((attack.action, attack.intention), ("attack", "cut_grass"))

    @staticmethod
    def grass_ledge_state(previous_rows=None):
        # Live map 129 tile (38,23): grass two rows above the hero, an open
        # row between that the game nevertheless refused (diagonal ledge).
        rows = ["#......", "...##..", ".....##", "...@.##",
                "#######", "#######", "#######"]
        attrs = ["13060000 12001600 12020000 12020000 12070000 12020000 12020000",
                 "12070000 12070000 12070000 1300c85f 1300c85f 12001700 12001400",
                 "12020000 12020000 12020000 12020000 12020000 73060000 73060000",
                 "12001700 12001400 12001700 12001400 12001700 73060000 73060000"] + \
            ["73060000 " * 6 + "73060000"] * 3
        return {"weapons_enabled": True, "enemy_slots": [],
                "party": [{"weapon_recharge": 0, "hp": 80, "max_hp": 100}],
                "tile_context": {
                    "runtime_verified": True, "game_mode": "field", "radius": 3,
                    "map_id": 129, "map_size_tiles": [64, 72], "actor_tile": [38, 23],
                    "actor": {"x_px": 623, "y_px": 370, "layer": 1},
                    "passability_rows": rows, "attribute_rows": attrs}}

    def test_cuts_grass_beyond_a_step_that_never_moves(self):
        planner = MemoryPlanner()
        planner.local_cuttable.add("1300c85f")
        planner.previous_action = "step_up"          # it kept pressing up
        state = self.grass_ledge_state()
        decisions = []
        for _ in range(8):
            planner.blocked_moves.clear()
            decision = planner._check_stall(state, PlannerDecisionStub("step_up"))
            decisions.append(decision)
        self.assertEqual(decisions[-1].action, "attack")
        self.assertEqual(decisions[-1].intention, "cut_grass")
        self.assertIn("38,21", decisions[-1].reason)

    def test_boxed_in_hero_cuts_grass_two_tiles_away(self):
        planner = MemoryPlanner()
        planner.local_cuttable.add("1300c85f")
        state = self.grass_ledge_state()
        # Everything around refused (stall-blocked); grass waits beyond.
        for action in ("step_up", "step_left", "step_right", "step_down"):
            planner.blocked_moves[planner._stall_key(state["tile_context"], action)] = 50
        decision = planner.decide(state, previous="step_up", directive="explore_north")
        self.assertEqual((decision.action, decision.intention), ("attack", "cut_grass"))

    def test_regrown_grass_on_a_walked_tile_is_cut_not_walked_into(self):
        planner = MemoryPlanner()
        planner.local_cuttable.add("1300c85f")
        planner.local_walked[131] = {(2, 4)}         # walked after an earlier cut
        state = observation(("...", ".@.", "###"))
        state["tile_context"]["attribute_rows"] = [
            "12020000 12020000 12020000", "12020000 12020000 12020000",
            "73060000 1300c85f 73060000"]
        self.assertNotIn("step_down", planner._passable_steps(state["tile_context"]))
        self.assertIn("step_down", planner.trusted_steps(state))     # as a cut tap
        state["tile_context"]["passability_rows"][0] = "###"
        state["tile_context"]["passability_rows"][1] = "#@#"
        decision = planner.decide(state, previous="step_down")
        self.assertEqual((decision.action, decision.intention), ("attack", "cut_grass"))

    def test_gives_up_on_grass_that_does_not_cut(self):
        planner = MemoryPlanner()
        planner.local_cuttable.add("03004000")
        grass = self.grass_state("#")
        previous, actions = "wait", []
        for _ in range(10):
            decision = planner.decide(grass, previous, "explore_west")
            actions.append(decision.action)
            previous = decision.action
        self.assertEqual(actions.count("attack"), 3)
        self.assertNotIn("step_left", planner.trusted_steps(grass))

    def test_history_learns_grass_and_maps_it_as_cuttable(self):
        with tempfile.TemporaryDirectory() as directory:
            history = ObservationHistory(Path(directory) / "grass.sqlite")
            self.assertEqual(history.learn_cuts(self.grass_state("#")), [])
            self.assertEqual(history.learn_cuts(
                self.grass_state(".", left_tile="11", left_attrs="00000000")), ["03004000"])
            other = self.grass_state("#")
            other["tile_context"]["map_id"] = 140
            history.remember_world(other)
            self.assertEqual(history.known_cells(140, 1)[(1, 3)], "cuttable")
            self.assertEqual(history.known_cells(140, 1)[(3, 3)], "blocked")
            history.close()

    def test_changes_that_are_not_cuts_are_ignored(self):
        from som_memory import detect_cuts
        before = self.grass_state("#")
        same_tile = self.grass_state(".", left_tile="10")       # tile did not change
        self.assertEqual(detect_cuts(before, same_tile), [])
        moved = self.grass_state(".", left_tile="11")
        moved["tile_context"]["map_id"] = 999                   # different map
        self.assertEqual(detect_cuts(before, moved), [])

    def test_log_bridge_route_follows_the_games_height_rules(self):
        # Row of the som-decomp log bridge (maps 128/130): land (type 2),
        # ramp 0x22 (type 0), end (type 1), deck (pass-through), end, ramp, land.
        row = ["h2", "open", "h1", "pass", "pass", "h1", "open", "h2"]
        cells = {(x, 0): kind for x, kind in enumerate(row)}
        routes = MemoryPlanner._routes((0, 0), cells, (16, 16), height=2)
        self.assertEqual(routes[(7, 0)], ("step_right", 7))
        # Without the ramp the raised bridge end is a wall from height 2.
        cells[(1, 0)] = "h2"
        routes = MemoryPlanner._routes((0, 0), cells, (16, 16), height=2)
        self.assertNotIn((2, 0), routes)
        # A river (type 2) next to the bridge end (type 1) is a wall from the deck.
        deck = {(0, 0): "pass", (1, 0): "h1", (1, 1): "h2"}
        routes = MemoryPlanner._routes((1, 0), deck, (16, 16), height=1)
        self.assertNotIn((1, 1), routes)

    def test_walked_bridge_deck_keeps_its_height_so_routes_do_not_swing(self):
        # Live map 130: hero on a log-bridge deck at height 1, a height-2 bank
        # below the deck. Walked tiles used to become "open" (height 0), so
        # the deck route stepped down onto the bank (refused by the game) and
        # two neighbouring tiles' routes pointed at each other.
        #   x: 0  1  2  3  4  5
        #   y0 o  h1 p  p  h1 o      (o open, p pass, h1/h2 raised)
        #   y1 o  h2 h2 h2 h2 o
        #   y2 o  o  o  o  o  o
        layout = {0: ["open", "h1", "pass", "pass", "h1", "open"],
                  1: ["open", "h2", "h2", "h2", "h2", "open"],
                  2: ["open"] * 6}
        with tempfile.TemporaryDirectory() as directory:
            history = ObservationHistory(Path(directory) / "deck.sqlite")
            for y, row in layout.items():
                for x, kind in enumerate(row):
                    history.connection.execute(
                        "INSERT INTO world_cells VALUES(130,1,?,?,?,1,0)", (x, y, kind))
            for x in range(6):
                history.connection.execute(
                    "INSERT INTO walked_cells VALUES(130,?,0,1,0)", (x,))
            history.connection.execute("INSERT INTO walked_cells VALUES(130,9,9,1,0)")
            history.connection.execute(
                "INSERT INTO world_cells VALUES(130,1,9,9,'blocked',1,0)")
            cells = history.known_cells(130, 1)
            self.assertEqual([cells[(x, 0)] for x in range(6)],
                             ["open", "h1", "pass", "pass", "h1", "open"])
            # A walked tile that only looked impassable keeps the arrival height.
            self.assertEqual(cells[(9, 9)], "pass")
            target = (1, 2)
            west = MemoryPlanner._routes((2, 0), cells, (16, 16), height=1)[target]
            east = MemoryPlanner._routes((3, 0), cells, (16, 16), height=1)[target]
            # Off the deck by its ramp end, never straight down onto the bank.
            self.assertEqual(west, ("step_left", 5))
            self.assertEqual(east, ("step_left", 6))
            history.close()

    @staticmethod
    def dead_end_state(hero_row=6):
        # 13x13 view: a wall 3 rows above the hero with a gap at the far right;
        # the enemy is straight up, beyond the wall.
        rows = ["............."] * 13
        rows[3] = "############."
        rows = [row if index != hero_row else row[:6] + "@" + row[7:]
                for index, row in enumerate(rows)]
        tile_y = 10 + hero_row - 6
        return {"weapons_enabled": True,
                "party": [{"weapon_recharge": 20, "hp": 80, "max_hp": 100}],
                "enemy_slots": [{"slot": 2, "active": True,
                                 "x_px": 10 * 16 + 8, "y_px": 4 * 16 + 8}],
                "tile_context": {
                    "runtime_verified": True, "game_mode": "field", "radius": 6,
                    "map_id": 131, "map_size_tiles": [32, 32], "actor_tile": [10, tile_y],
                    "actor": {"x_px": 10 * 16 + 8, "y_px": tile_y * 16 + 8, "layer": 1},
                    "passability_rows": rows}}

    def test_walled_enemy_is_reached_by_the_route_not_the_dead_end(self):
        planner = MemoryPlanner()
        for hero_row in (6, 5, 4, 6, 5):
            decision = planner.decide(self.dead_end_state(hero_row))
            self.assertEqual(decision.intention, "engage_enemy")
            self.assertEqual(decision.action, "step_right",
                             f"row {hero_row}: {decision.reason}")

    def test_combat_cuts_known_grass_directly_between_hero_and_enemy(self):
        planner = MemoryPlanner()
        planner.local_cuttable.add("03004000")
        state = observation(
            ("###", "#@#", "###"),
            enemy={"slot": 1, "active": True, "x_px": 40, "y_px": 24},
            recharge=0)
        state["tile_context"]["attribute_rows"] = [
            "00000000 03004000 00000000",
            "00000000 00000000 00000000",
            "00000000 00000000 00000000"]
        face = planner.decide(state, previous="wait")
        self.assertEqual((face.action, face.intention), ("step_up", "cut_grass"))
        swing = planner.decide(state, previous=face.action)
        self.assertEqual((swing.action, swing.intention), ("attack", "cut_grass"))

    def test_combat_does_not_cut_unknown_wall_or_side_grass(self):
        planner = MemoryPlanner()
        state = observation(
            ("###", "#@#", "###"),
            enemy={"slot": 1, "active": True, "x_px": 40, "y_px": 24},
            recharge=0)
        state["tile_context"]["attribute_rows"] = [
            "00000000 03004000 00000000",
            "03004000 00000000 00000000",
            "00000000 00000000 00000000"]
        self.assertIsNone(planner._grass_blocking_enemy(
            state["tile_context"], 0, -32))
        planner.local_cuttable.add("03004000")
        self.assertEqual(planner._grass_blocking_enemy(
            state["tile_context"], 0, -32), "step_up")

    def test_enemy_far_off_screen_is_not_chased(self):
        state = observation(enemy={"slot": 1, "active": True, "x_px": 40 + 380, "y_px": 56})
        state["tile_context"]["map_size_tiles"] = [64, 64]
        self.assertNotEqual(MemoryPlanner().decide(state).intention, "engage_enemy")

    def test_route_reports_how_far_it_runs_straight(self):
        # Corridor: right 2 tiles, then up 5.
        cells = {(x, 9): "open" for x in range(0, 3)}
        cells.update({(2, y): "open" for y in range(4, 9)})
        routes = MemoryPlanner._routes((0, 9), cells, (16, 16))
        self.assertEqual(routes[(2, 4)][0], "step_right")
        self.assertEqual(routes.straight((2, 4)), 2)
        cells.update({(x, 9): "open" for x in range(3, 9)})
        routes = MemoryPlanner._routes((0, 9), cells, (16, 16))
        self.assertEqual(routes.straight((8, 9)), 8)

    def test_low_health_uses_learned_safe_route_without_fighting(self):
        with tempfile.TemporaryDirectory() as directory:
            history = ObservationHistory(Path(directory) / "safe.sqlite")
            history.set_agent_state("safe_anchor",
                                    {"map_id": 131, "layer": 1, "target": [1, 3]})
            planner = MemoryPlanner(history)
            enemy = {"slot": 1, "active": True, "x_px": 120, "y_px": 56}
            retreat = planner.decide(observation(enemy=enemy, hp=20, max_hp=100))
            route = planner.decide(observation(hp=20, max_hp=100), retreat.action)
            self.assertEqual(retreat.intention, "return_to_inn")
            self.assertEqual(route.action, "step_left")
            self.assertEqual(route.intention, "return_to_inn")
            history.close()

    def test_low_health_keeps_heading_to_disconnected_safe_target(self):
        with tempfile.TemporaryDirectory() as directory:
            history = ObservationHistory(Path(directory) / "safe-heading.sqlite")
            history.set_agent_state("safe_anchor",
                                    {"map_id": 131, "layer": 1, "target": [7, 3]})
            planner = MemoryPlanner(history)
            decision = planner.decide(observation(hp=20, max_hp=100))
            self.assertEqual(decision.action, "step_left")
            self.assertIn("learned inn", decision.reason)
            history.close()

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

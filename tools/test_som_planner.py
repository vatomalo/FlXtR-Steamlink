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

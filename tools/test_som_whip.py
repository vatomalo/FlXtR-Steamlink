"""Whip crossings (whip posts over ledges): learning and use. No emulator."""
import tempfile
import unittest
from pathlib import Path

from som_memory import ObservationHistory, detect_whip_crossing, whip_post_signature
from som_planner import MemoryPlanner, PlannerDecision

POST = "1300c40a"        # a special tile (b2 0xC4) standing at the ledge
WALL = "13060000"        # plain solid
HIGH = "12020000"        # floor height 2
LOW = "11020000"         # floor height 1

# 7x7 grid, hero (centre) on a height-2 shelf; two rows of cliff below,
# then height-1 floor that the hero cannot walk to.
LEDGE_ROWS = ["...@...".replace("@", "."), ".......", ".......", "...@...",
              "#######", "#######", "#######"]
LEDGE_TERRAIN = ["2222222", "2222222", "2222222", "2222222",
                 "#######", "#######", "1111111"]
LEDGE_ATTRS = [" ".join([HIGH] * 7)] * 4 + [
    " ".join([WALL] * 3 + [POST] + [WALL] * 3),
    " ".join([WALL] * 7),
    " ".join([LOW] * 7)]


def state(tile, rows=LEDGE_ROWS, terrain=LEDGE_TERRAIN, attrs=LEDGE_ATTRS, weapon="whip",
          owned=("sword", "whip"), height=2, map_id=30, airborne=False):
    special = [{"offset": [0, 0], "classification": "blocked", "reasons": ["solid"],
                "attributes": WALL}] if airborne else []
    return {
        "weapons_enabled": True, "enemy_slots": [], "owned_weapons": list(owned),
        "party": [{"weapon_recharge": 0, "hp": 80, "max_hp": 100,
                   "weapon": {"type": weapon}}],
        "tile_context": {
            "runtime_verified": True, "game_mode": "field", "radius": 3,
            "map_id": map_id, "map_size_tiles": [64, 64], "actor_tile": list(tile),
            "actor": {"x_px": tile[0] * 16 + 8, "y_px": tile[1] * 16 + 10,
                      "layer": 1, "floor_height": height},
            "passability_rows": list(rows), "terrain_rows": list(terrain),
            "attribute_rows": list(attrs),
            "tile_id_rows": [" ".join(["01"] * 7)] * 7,
            "special_cells": special,
        },
    }


def landed(tile):
    """The same ground seen from the landing tile below the cliff."""
    rows = ["#######", "#######", "#######", "...@...", ".......", ".......", "......."]
    terrain = ["2222222", "#######", "#######", "1111111", "1111111", "1111111", "1111111"]
    attrs = [" ".join([HIGH] * 7), " ".join([WALL] * 3 + [POST] + [WALL] * 3),
             " ".join([WALL] * 7)] + [" ".join([LOW] * 7)] * 4
    return state(tile, rows, terrain, attrs, height=1)


class TestDetectWhipCrossing(unittest.TestCase):
    def test_pull_over_a_ledge_is_a_crossing_with_its_post(self):
        crossing = detect_whip_crossing(state((10, 10)), landed((10, 13)))
        self.assertEqual((crossing["from"], crossing["action"], crossing["to"]),
                         ([10, 10], "step_down", [10, 13]))
        self.assertEqual(crossing["posts"], [POST])
        self.assertEqual(crossing["to_height"], 1)

    def test_without_the_whip_in_hand_it_is_not_a_crossing(self):
        self.assertIsNone(detect_whip_crossing(state((10, 10), weapon="sword"),
                                               landed((10, 13))))

    def test_a_short_walk_around_one_rock_is_not_a_crossing(self):
        rows = [".......", ".......", ".......", "...@...", "...#...", ".......", "......."]
        terrain = ["2222222"] * 4 + ["222#222", "2222222", "2222222"]
        before = state((10, 10), rows, terrain)
        after = state((10, 12), rows, terrain)
        self.assertIsNone(detect_whip_crossing(before, after))

    def test_plain_walls_and_grass_are_not_post_candidates(self):
        self.assertTrue(whip_post_signature(POST))
        self.assertFalse(whip_post_signature(WALL))
        self.assertFalse(whip_post_signature("1300c85f"))   # Potos bushes (cut code)
        self.assertFalse(whip_post_signature("12001700"))   # diagonal half tile

    def test_history_skips_mid_flight_samples_and_stores_the_crossing(self):
        with tempfile.TemporaryDirectory() as folder:
            history = ObservationHistory(Path(folder) / "h.sqlite")
            ticks = iter([0.0, 0.3, 0.6])
            clock = lambda: next(ticks)
            self.assertIsNone(history.learn_whip_crossings(state((10, 10)), clock))
            # Over the cliff: the hero's own tile is a wall, not a landing.
            self.assertIsNone(history.learn_whip_crossings(
                state((10, 11), airborne=True), clock))
            crossing = history.learn_whip_crossings(landed((10, 13)), clock)
            self.assertEqual(crossing["from"], [10, 10])
            self.assertEqual(history.whip_crossings(30, 1),
                             [{"from": (10, 10), "action": "step_down", "to": (10, 13),
                               "to_layer": 1, "to_height": 1}])
            self.assertEqual(history.whip_post_signatures(), {POST})
            history.close()


class TestPlannerUsesTheWhip(unittest.TestCase):
    def stall(self, planner, observation, action="step_down"):
        planner.previous_action = action
        planner.whip_ready = "whip" in observation["owned_weapons"]
        decision = None
        for _ in range(8):
            decision = planner._check_stall(observation,
                                            PlannerDecision(action, "test", "explore_frontier"))
        return decision

    def test_refused_step_at_a_ledge_probes_with_the_whip_once(self):
        planner = MemoryPlanner()
        decision = self.stall(planner, state((10, 10)))
        self.assertEqual((decision.action, decision.intention), ("attack", "whip_cross"))
        self.assertIn("probe", decision.reason)
        planner.blocked_moves.clear()
        again = self.stall(planner, state((10, 10)))
        self.assertEqual(again.action, "wait")

    def test_no_whip_owned_means_no_swing(self):
        decision = self.stall(MemoryPlanner(), state((10, 10), owned=("sword",)))
        self.assertEqual(decision.action, "wait")

    def test_a_recognised_post_gets_several_swings(self):
        planner = MemoryPlanner()
        planner.local_posts.add(POST)
        decision = self.stall(planner, state((10, 10), weapon="sword"))
        self.assertEqual(decision.intention, "whip_cross")
        self.assertIn("whip post", decision.reason)
        self.assertIn("1/3", decision.reason)

    def test_the_recorded_map30_post_is_known_without_learning(self):
        # Map 30, 2026-11-16: hero on the trigger tile (28,8) facing a
        # 3-tile gap (BA walls); post A1 (13 07 F8 6F) at (32,8), floor past it.
        gap, post, floor, trigger = "13060000", "1307f86f", "12060000", "10800000"
        row = [floor] * 5 + [trigger] + [gap] * 3 + [post] + [floor]
        attrs = [" ".join([floor] * 11)] * 5 + [" ".join(row[0:11])] + [" ".join([floor] * 11)] * 5
        rows = ["..........."] * 5 + ["....." + "@###" + "#."] + ["..........."] * 5
        terrain = ["22222222222"] * 5 + ["22222" + "2###" + "#2"] + ["22222222222"] * 5
        observation = state((28, 8), rows, terrain, attrs, weapon="sword")
        observation["tile_context"]["radius"] = 5
        planner = MemoryPlanner()
        planner.whip_ready = True
        options = planner._whip_options(observation["tile_context"])
        self.assertIn("step_right", options)
        self.assertIn("whip post", options["step_right"])

    def test_route_uses_a_learned_crossing(self):
        cells = {(x, y): "h2" for x in range(8, 13) for y in range(7, 11)}
        cells.update({(x, y): "h1" for x in range(8, 13) for y in range(13, 16)})
        jumps = {(10, 10): [("step_down", (10, 13), 1)]}
        routes = MemoryPlanner._routes((10, 10), cells, (64, 64), 2, jumps=jumps)
        self.assertEqual(routes[(10, 15)][0], "whip:step_down")
        weighted = MemoryPlanner._routes((10, 10), cells, (64, 64), 2, {(10, 13): 1},
                                         jumps=jumps)
        self.assertEqual(weighted[(10, 15)][0], "whip:step_down")
        self.assertNotIn((10, 15), MemoryPlanner._routes((10, 10), cells, (64, 64), 2))

    def test_explores_beyond_a_learned_crossing_face_then_swing(self):
        planner = MemoryPlanner()
        planner.local_whip[(30, 1)] = {((10, 10), "step_down"): {
            "from": [10, 10], "action": "step_down", "to": [10, 13],
            "to_layer": 1, "to_height": 1}}
        face = planner.decide(state((10, 10), weapon="sword"), "wait", "explore_south")
        self.assertEqual((face.action, face.intention, face.turn_only),
                         ("step_down", "whip_cross", True))
        swing = planner.decide(state((10, 10), weapon="sword"), "step_down", "explore_south")
        self.assertEqual((swing.action, swing.intention), ("attack", "whip_cross"))


if __name__ == "__main__":
    unittest.main()

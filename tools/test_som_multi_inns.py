"""Multiple verified inns: Potos is NOT a universal destination.

No ROM, Ollama, SSH, or live device required.
"""
import tempfile
import unittest
from pathlib import Path

from som_memory import ObservationHistory
from som_goals import memory_summary, offered_goals
from som_planner import MemoryPlanner


def field(map_id, hp=100, max_hp=100, tile=(2, 3), layer=1):
    return {
        "party": [{"hp": hp, "max_hp": max_hp}],
        "enemy_slots": [],
        "tile_context": {
            "runtime_verified": True, "game_mode": "field",
            "map_id": map_id, "actor_tile": list(tile),
            "map_size_tiles": [16, 16],
            "actor": {"x_px": tile[0] * 16 + 8, "y_px": tile[1] * 16 + 8,
                      "layer": layer},
            "passability_rows": [".....", ".....", "..@..", ".....", "....."],
            "radius": 2,
        },
    }


class MultiInnTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.path = Path(self.temp.name) / "inns.sqlite"
        self.history = ObservationHistory(self.path)

    def tearDown(self):
        self.history.close()
        self.temp.cleanup()

    def teach(self, map_id, face="step_up", tile=(2, 3)):
        return self.history.teach_inn(field(map_id, tile=tile), face)

    def edge(self, source, destination):
        self.history.connection.execute(
            "INSERT INTO map_edges "
            "(from_map,to_map,layer,x,y,action,seen,last_ts) "
            "VALUES(?,?,1,1,1,'step_right',1,0)",
            (source, destination))
        self.history.connection.commit()

    def test_taught_local_inn_beats_globally_last_taught_anchor(self):
        potos = self.teach(131, "step_up", (9, 9))
        local = self.teach(150, "step_left")
        self.assertEqual(len(self.history.known_inns()), 2)
        self.assertEqual(self.history.safe_anchor(), local)  # legacy single pointer
        self.assertEqual(self.history.select_inn(131), potos)
        self.assertEqual(self.history.select_inn(150), local)
        # No route: no detour to Potos and no fabricated connectivity.
        self.assertIsNone(self.history.select_inn(151))
        self.history.close()
        self.history = ObservationHistory(self.path)
        self.assertEqual(self.history.select_inn(131), potos)
        self.assertEqual(self.history.select_inn(150), local)

    def test_shortest_learned_map_route_picks_other_town_inn(self):
        self.teach(131, "step_up", (8, 9))
        local = self.teach(150, "step_left")
        self.edge(151, 150)
        self.edge(151, 149)
        self.edge(149, 131)
        self.assertEqual(self.history.select_inn(151), local)
        self.assertIsNone(self.history.select_inn(999))
        facts = memory_summary(self.history, field(151, hp=35))
        self.assertEqual(facts["inn_map"], 150)
        self.assertTrue(facts["inn_taught"])
        self.assertIn("go_to_inn", offered_goals(facts, reachable=lambda m: m == 150))

    def test_legacy_taught_potos_inn_is_migrated_not_lost(self):
        old = {"map_id": 131, "layer": 1, "target": [6, 6],
               "face": "step_right", "taught": True}
        self.history.set_agent_state("safe_anchor", old)
        self.assertEqual(self.history.select_inn(131), old)
        self.assertEqual(self.history.known_inns(), [old])
        self.teach(150)
        self.assertEqual(len(self.history.known_inns()), 2)
        self.assertEqual(self.history.select_inn(131), old)

    def test_inn_heal_does_not_overwrite_taught_inn_or_invent_macro(self):
        potos = self.teach(131)
        self.history.remember_recovery(field(150, hp=30))
        detected = self.history.remember_recovery(field(150, hp=100))
        self.assertIsNotNone(detected)
        self.assertEqual(self.history.safe_anchor(), potos)
        self.assertEqual(self.history.select_inn(150, require_taught=False)["map_id"], 150)
        self.assertIsNone(self.history.select_inn(150, require_taught=True))
        self.assertEqual(len(self.history.known_inns()), 2)

    def test_generic_full_health_potos_fallback_is_not_a_taught_inn(self):
        self.history.set_agent_state(
            "safe_anchor", {"map_id": 131, "layer": 1, "target": [3, 4]})
        self.assertEqual(self.history.known_inns(), [])
        self.assertIsNone(self.history.select_inn(131))
        facts = memory_summary(self.history, field(131, hp=35))
        self.assertFalse(facts["inn_taught"])
        self.assertNotIn("go_to_inn", offered_goals(facts, reachable=lambda m: True))

    def test_survival_reaches_local_taught_inn_even_when_potos_was_last_taught(self):
        local = self.teach(150, "step_left")
        self.teach(131, "step_up")
        # Potos is the legacy pointer, but emergency survival is on map 150.
        self.assertEqual(self.history.safe_anchor()["map_id"], 131)
        planner = MemoryPlanner(self.history)
        state = field(150, hp=30, tile=tuple(local["target"]))
        decision = planner._survive(state, state["tile_context"],
                                    ["step_up", "step_down", "step_left", "step_right"])
        self.assertEqual(decision.intention, "recover_at_inn")
        self.assertIn("learned inn", decision.reason)


if __name__ == "__main__":
    unittest.main()

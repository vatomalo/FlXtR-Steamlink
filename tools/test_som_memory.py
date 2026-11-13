"""Unit tests: synthetic raw WRAM, no ROM, emulator, or network required."""
import json
import tempfile
import unittest
from pathlib import Path

from som_memory import WRAM, ObservationHistory


def put16(data, address, value):
    offset = address - 0x7E0000
    data[offset:offset + 2] = value.to_bytes(2, "little")


class TestWRAM(unittest.TestCase):
    def test_floor_height_collision_classification(self):
        self.assertEqual(WRAM.classify_tile([1, 0, 0, 0], 1)[0], "open")
        self.assertEqual(WRAM.classify_tile([2, 0, 0, 0], 1),
                         ("blocked", ["floor_height_wall"]))

    def test_requires_raw_wram_not_savestate(self):
        with self.assertRaises(ValueError):
            WRAM(b"savestate")

    def test_bounds(self):
        reader = WRAM(bytes(128 * 1024))
        with self.assertRaises(ValueError):
            reader.u8(0x7DFFFF)
        with self.assertRaises(ValueError):
            reader.u16(0x7FFFFF)

    def test_decodes_documented_fields(self):
        data = bytearray(128 * 1024)
        data[0x7E00ED - 0x7E0000] = 0x80
        data[0x7EE000 - 0x7E0000] = 1
        data[0x7EE180 - 0x7E0000] = 0x80
        data[0x7EE181 - 0x7E0000] = 12
        data[0x7EE02C - 0x7E0000] = 1
        data[0x7ECC73 - 0x7E0000] = 3
        data[0x7ECC7A - 0x7E0000] = 15
        data[0x7ECC7D - 0x7E0000] = 2
        put16(data, 0x7EE182, 77)
        put16(data, 0x7EE184, 100)
        put16(data, 0x7EE602, 18)
        put16(data, 0x7EE604, 25)
        data[0x7EE600 - 0x7E0000] = 1
        data[0x7EE780 - 0x7E0000] = 0x20
        put16(data, 0x7EE782, 10)
        put16(data, 0x7EE784, 30)
        state = WRAM(data).observe()
        self.assertTrue(state["weapons_enabled"])
        self.assertEqual(state["party"][0]["graphic"], "boy")
        self.assertEqual(state["party"][0]["hp"], 77)
        self.assertEqual(state["party"][0]["controller_binding"], 1)
        self.assertEqual(state["party"][0]["native_ai_behavior"], 3)
        self.assertEqual(state["party"][0]["native_ai_grid_cell"], 15)
        self.assertEqual(state["party"][0]["native_ai_charge_level"], 2)
        self.assertEqual(state["enemy_slots"][0]["x_px"], 18)
        self.assertEqual(state["enemy_slots"][0]["y_px"], 25)
        self.assertTrue(state["enemy_slots"][0]["active"])
        self.assertIsNone(state["player_position"])

    def test_decodes_candy_only_from_documented_valid_inventory_byte(self):
        data = bytearray([0xFF]) * (128 * 1024)
        data[0x7ECC48 - 0x7E0000] = 0x40
        reader = WRAM(data)
        self.assertEqual(reader.inventory()[0]["name"], "candy")
        self.assertEqual(reader.observe()["candy_count"], 2)
        data[0x7ECC48 - 0x7E0000] = 0x30
        self.assertIsNone(WRAM(data).inventory()[0]["count"])

    def test_runtime_verifies_and_classifies_nearby_tiles(self):
        data = bytearray(128 * 1024)
        data[0x7EE000 - 0x7E0000] = 1
        data[0x7E00E4 - 0x7E0000] = 1
        data[0x7E00E6 - 0x7E0000] = 0x0F
        put16(data, 0x7E00C0, 16 * 8)
        put16(data, 0x7E00C2, 16 * 8)
        put16(data, 0x7E00DC, 131)
        put16(data, 0x7EE002, 2 * 16 + 8)
        put16(data, 0x7EE004, 3 * 16 + 8)
        data[0x7EE00B - 0x7E0000] = 0
        data[0x7EE04C - 0x7E0000] = 5
        data[0x7EE048 - 0x7E0000] = 0
        data[0x7EE08E - 0x7E0000] = 6
        tile_map = 0x7F0000 - 0x7E0000
        data[tile_map:tile_map + 0x4000] = bytes([5]) * 0x4000
        data[tile_map + 3 * 128 + 3] = 6
        attributes = 0x7FB800 - 0x7E0000
        data[attributes + 5 * 4:attributes + 5 * 4 + 4] = bytes([0, 6, 0, 0])
        data[attributes + 6 * 4:attributes + 6 * 4 + 4] = bytes([3, 0, 0, 0])
        state = WRAM(data).observe()
        context = state["tile_context"]
        self.assertTrue(context["runtime_verified"])
        self.assertEqual(context["map_id"], 131)
        self.assertEqual(context["game_mode"], "field")
        self.assertEqual(context["actor_tile"], [2, 3])
        self.assertEqual(context["passability_rows"][3][3:5], "@#")
        self.assertEqual(state["player_position"]["x_px"], 40)

    def test_tile_context_accepts_stale_id_when_collision_fields_agree(self):
        data = bytearray(128 * 1024)
        data[0x7EE000 - 0x7E0000] = 1
        data[0x7E00E4 - 0x7E0000] = 1
        data[0x7E00E6 - 0x7E0000] = 0x0F
        put16(data, 0x7E00C0, 16 * 8)
        put16(data, 0x7E00C2, 16 * 8)
        data[0x7EE04C - 0x7E0000] = 99
        context = WRAM(data).tile_context()
        self.assertTrue(context["runtime_verified"])
        self.assertEqual(context["verification_warning"], "stale_actor_tile_id_cache")

    def test_tile_context_fails_closed_when_collision_fields_disagree(self):
        data = bytearray(128 * 1024)
        data[0x7EE000 - 0x7E0000] = 1
        data[0x7E00E4 - 0x7E0000] = 1
        data[0x7E00E6 - 0x7E0000] = 0x0F
        put16(data, 0x7E00C0, 16 * 8)
        put16(data, 0x7E00C2, 16 * 8)
        data[0x7EE08E - 0x7E0000] = 1
        data[0x7EE048 - 0x7E0000] = 1
        context = WRAM(data).tile_context()
        self.assertFalse(context["runtime_verified"])
        self.assertEqual(context["verification_error"], "actor_tile_attributes_disagree")
        self.assertNotIn("passability_rows", context)

    def test_tile_context_accepts_stale_priority_on_matching_special_ground(self):
        data = bytearray(128 * 1024)
        data[0x7EE000 - 0x7E0000] = 1
        data[0x7E00E4 - 0x7E0000] = 1
        data[0x7E00E6 - 0x7E0000] = 0x0F
        put16(data, 0x7E00C0, 16 * 8)
        put16(data, 0x7E00C2, 16 * 8)
        data[0x7EE00B - 0x7E0000] = 2
        data[0x7EE04C - 0x7E0000] = 0
        data[0x7EE048 - 0x7E0000] = 0x17
        data[0x7EE08E - 0x7E0000] = 2
        attributes = 0x7FB800 - 0x7E0000
        data[attributes:attributes + 4] = bytes([0x12, 0, 0x17, 0])
        context = WRAM(data).tile_context()
        self.assertTrue(context["runtime_verified"])
        self.assertEqual(context["verification_warning"],
                         "stale_actor_priority_cache")

    def test_history(self):
        with tempfile.TemporaryDirectory() as directory:
            history = ObservationHistory(Path(directory) / "history.sqlite")
            history.record({"player_position": None}, "right")
            self.assertEqual(history.recent()[0]["action"], "right")
            history.close()

    def test_combat_history_learns_observed_damage(self):
        with tempfile.TemporaryDirectory() as directory:
            history = ObservationHistory(Path(directory) / "combat.sqlite")
            state = {
                "map_id": 7, "player_position": {"x_px": 10, "y_px": 20},
                "party": [{"hp": 80, "max_hp": 100, "level": 4}],
                "enemy_slots": [{"slot": 1, "active": True, "x_px": 30,
                                 "y_px": 20, "hp": 40}],
            }
            history.record_combat(state, "step_right")
            state["party"][0]["hp"] = 75
            state["enemy_slots"][0]["hp"] = 28
            history.record_combat(state, "attack")
            attack = next(row for row in history.combat_summary()
                          if row["action"] == "attack")
            self.assertEqual(attack["damage_dealt"], 12)
            self.assertEqual(attack["damage_taken"], 5)
            self.assertEqual(attack["closest_px"], 20)
            history.close()

    def test_recovery_location_becomes_safe_anchor(self):
        with tempfile.TemporaryDirectory() as directory:
            history = ObservationHistory(Path(directory) / "safety.sqlite")
            state = {
                "party": [{"hp": 30, "max_hp": 100}], "enemy_slots": [],
                "tile_context": {"runtime_verified": True, "map_id": 257,
                                 "actor_tile": [4, 5], "actor": {"layer": 1}},
            }
            history.remember_recovery(state)
            state["party"][0]["hp"] = 100
            learned = history.remember_recovery(state)
            self.assertEqual(learned["target"], [4, 5])
            self.assertEqual(history.safe_anchor()["map_id"], 257)
            self.assertEqual(history.place_summary(257)[0]["kind"], "inn")
            atlas = history.export_map_json()
            self.assertEqual(json.loads(atlas.read_text())["format"],
                             "flxtr-som-atlas-v1")
            history.close()

    def test_candy_heal_and_later_heals_never_move_a_taught_inn(self):
        with tempfile.TemporaryDirectory() as directory:
            history = ObservationHistory(Path(directory) / "taught.sqlite")
            state = {
                "party": [{"hp": 30, "max_hp": 100}], "enemy_slots": [], "candy_count": 2,
                "tile_context": {"runtime_verified": True, "map_id": 128,
                                 "actor_tile": [4, 5], "actor": {"layer": 1}},
            }
            history.remember_recovery(state)
            state.update(party=[{"hp": 100, "max_hp": 100}], candy_count=1)
            self.assertIsNone(history.remember_recovery(state))
            inn = dict(state, tile_context=dict(state["tile_context"], map_id=131,
                                                actor_tile=[9, 9]))
            history.teach_inn(inn, "step_up")
            state.update(party=[{"hp": 30, "max_hp": 100}])
            history.remember_recovery(state)
            state.update(party=[{"hp": 100, "max_hp": 100}])
            history.remember_recovery(state)
            anchor = history.safe_anchor()
            self.assertEqual((anchor["map_id"], anchor["target"], anchor["face"]),
                             (131, [9, 9], "step_up"))
            history.close()

    def test_inn_lookalike_is_recognised_on_another_map_only(self):
        with tempfile.TemporaryDirectory() as directory:
            history = ObservationHistory(Path(directory) / "pattern.sqlite")
            rows = [" ".join(f"{(r * 7 + c):02x}" for c in range(7)) for r in range(7)]
            def state(map_id, tile_rows):
                return {"party": [{"hp": 10, "max_hp": 100}], "enemy_slots": [],
                        "tile_context": {"runtime_verified": True, "map_id": map_id,
                                         "actor_tile": [4, 4], "actor": {"layer": 1},
                                         "tile_id_rows": tile_rows}}
            history.teach_inn(state(131, rows), "step_up")
            self.assertIsNone(history.looks_like("inn", state(131, rows)))
            similar = rows[:6] + ["ff ff ff ff ff ff ff"]
            self.assertAlmostEqual(history.looks_like("inn", state(150, similar)), 42 / 49)
            different = ["00 00 00 00 00 00 00"] * 7
            self.assertIsNone(history.looks_like("inn", state(150, different)))
            history.close()

    def test_tile_rules_match_the_game(self):
        classify, code = WRAM.classify_tile, WRAM.terrain_code
        self.assertEqual(classify([0x10, 0x07, 0x22, 0], 2)[0], "open")    # bridge ramp
        self.assertEqual(classify([0x15, 0x00, 0x00, 0], 1)[0], "open")    # bridge deck
        self.assertEqual(classify([0x11, 0x07, 0x00, 0], 2)[0], "blocked") # end from bank
        self.assertEqual(classify([0x11, 0x07, 0x00, 0], 0)[0], "open")    # end from ramp
        self.assertEqual(classify([0x13, 0x06, 0x00, 0], 0)[0], "blocked") # solid
        self.assertEqual(classify([0x18, 0x00, 0x00, 0], 1)[0], "open")    # layer switch
        self.assertEqual(classify([0x10, 0x00, 0x14, 0], 0)[0], "open")    # diagonal slide
        self.assertEqual(classify([0x10, 0x00, 0x31, 0], 0)[0], "conditional")
        self.assertEqual([code(a) for a in ([0x10, 7, 0x22, 0], [0x11, 7, 0, 0],
                                            [0x15, 0, 0, 0], [0x13, 6, 0, 0],
                                            [0x12, 2, 0, 0])],
                         [".", "1", "=", "#", "2"])

    def test_return_waypoint_reverses_observed_map_transition(self):
        with tempfile.TemporaryDirectory() as directory:
            history = ObservationHistory(Path(directory) / "routes.sqlite")
            def state(map_id, tile):
                return {"tile_context": {"runtime_verified": True, "map_id": map_id,
                                         "actor_tile": tile, "actor": {"layer": 1}}}
            history.record(state(131, [2, 3]), "step_left")
            history.record(state(130, [7, 3]), "wait")
            waypoint = history.return_waypoint(130, 131)
            self.assertEqual(waypoint["target"], [7, 3])
            self.assertEqual(waypoint["action"], "step_right")
            history.close()

    def test_route_home_survives_log_trimming_and_spans_several_maps(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "multi.sqlite"
            history = ObservationHistory(path)
            def state(map_id, tile):
                return {"tile_context": {"runtime_verified": True, "map_id": map_id,
                                         "actor_tile": tile, "actor": {"layer": 1}}}
            # Potos (131) -> 130 -> 129, then thousands of decisions in 129.
            history.record(state(131, [2, 3]), "step_left")
            history.record(state(130, [7, 3]), "step_up")
            history.record(state(130, [7, 0]), "step_up")
            history.record(state(129, [5, 9]), "wait")
            history.connection.execute("DELETE FROM observations")
            history.connection.commit()
            waypoint = history.return_waypoint(129, 131)
            self.assertEqual(waypoint["target"], [5, 9])
            self.assertEqual(waypoint["action"], "step_down")
            self.assertEqual(waypoint["next_map"], 130)
            self.assertEqual(history.return_waypoint(130, 131)["action"], "step_right")
            history.close()
            # Existing databases are backfilled once from their observation log.
            legacy = Path(directory) / "legacy.sqlite"
            old = ObservationHistory(legacy)
            old.record(state(131, [2, 3]), "step_left")
            old.record(state(130, [7, 3]), "wait")
            old.connection.execute("DELETE FROM map_edges")
            old.connection.commit()
            old.close()
            reopened = ObservationHistory(legacy)
            self.assertEqual(reopened.return_waypoint(130, 131)["target"], [7, 3])
            reopened.close()

    def test_safe_anchor_falls_back_to_earliest_verified_potos_position(self):
        with tempfile.TemporaryDirectory() as directory:
            history = ObservationHistory(Path(directory) / "anchor.sqlite")
            state = {"party": [{"hp": 20, "max_hp": 100}],
                     "enemy_slots": [{"active": True}],
                     "tile_context": {"runtime_verified": True, "map_id": 131,
                                      "actor_tile": [9, 4], "actor": {"layer": 1}}}
            history.record(state, "step_left")
            self.assertEqual(history.safe_anchor()["target"], [9, 4])
            history.close()


if __name__ == "__main__":
    unittest.main()

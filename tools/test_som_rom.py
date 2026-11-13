"""ROM exit table, map-load guard and multi-map routing tests (synthetic data)."""
import tempfile
import unittest
from pathlib import Path

from som_memory import MapLoadGuard, ObservationHistory, WRAM
from som_planner import MemoryPlanner
from som_rom import BANK_C8, RomLayoutError, RomTables, load_tables, scan_exit_tiles

MAPS_WITH_HEADERS = 200


def make_rom(exits=None, relocate=None, header=0):
    """Synthetic image: maps 0..199 have headers with an entry event.

    exits: {map: [event word per trigger tile 0xC0, 0xC1, ...]}
    relocate: map whose event list pointer jumps backwards (Turbo-like).
    """
    exits = exits or {}
    rom = bytearray(0x100000)

    def put_word(offset, value):
        rom[BANK_C8 + offset] = value & 0xFF
        rom[BANK_C8 + offset + 1] = value >> 8

    for m in range(0x200):
        put_word(0x7000 + 2 * m, 0x7400 + 8 * min(m, MAPS_WITH_HEADERS))
    for m in range(MAPS_WITH_HEADERS):
        rom[BANK_C8 + 0x7400 + 8 * m + 3] = 4        # entry event, base 0xC0
    cursor = 0x4400
    for m in range(0x200):
        put_word(0x4000 + 2 * m, cursor)
        words = [0x700] + exits.get(m, []) if m < MAPS_WITH_HEADERS else []
        for word in words:
            put_word(cursor, word)
            cursor += 2
    if relocate is not None:
        put_word(0x4000 + 2 * relocate, 0x4400)
    for n, (target, x, y, layer2) in enumerate([(130, 27, 31, 0), (129, 18, 61, 0),
                                                (131, 10, 23, 0), (128, 41, 51, 0),
                                                (257, 15, 15, 1)]):
        offset = BANK_C8 + 0x3000 + 4 * n
        word = target | x << 9
        rom[offset], rom[offset + 1] = word & 0xFF, word >> 8
        rom[offset + 2] = y << 1 | layer2
    return bytes(header * [0]) + bytes(rom)


def potos_rom():
    # 131 -> 130 (transition 0), 130 -> 129 (1), 130 -> 131 (2), 129 -> 128 (3),
    # 131 -> 257 via a script word (0x45A, not followed).
    return make_rom({131: [0x800, 0x45A], 130: [0x801, 0x802], 129: [0x803]})


def wram_with_tiles(width, height, tiles, layer=1):
    data = bytearray(0x20000)
    data[0xC0:0xC2] = (width * 16).to_bytes(2, "little")
    data[0xC2:0xC4] = (height * 16).to_bytes(2, "little")
    base = 0x10000 if layer == 1 else 0x14000
    for (x, y), tile_id in tiles.items():
        data[base + y * 128 + x] = tile_id
    return WRAM(bytes(data))


def crossing_state(map_id, tile, trigger_offset=None, trigger_id=0xC0):
    """Verified observation on map_id; a trigger id next to the hero."""
    rows = ["00 00 00 00 00 00 00"] * 7
    if trigger_offset is not None:
        dx, dy = trigger_offset
        cells = rows[3 + dy].split()
        cells[3 + dx] = f"{trigger_id:02x}"
        rows = list(rows)
        rows[3 + dy] = " ".join(cells)
    return {"tile_context": {
        "runtime_verified": True, "game_mode": "field", "map_id": map_id,
        "radius": 3, "actor_tile": list(tile), "map_size_tiles": [40, 48],
        "actor": {"layer": 1, "x_px": tile[0] * 16 + 8, "y_px": tile[1] * 16 + 8},
        "tile_id_rows": rows, "passability_rows": ["......."] * 7}}


class TestRomTables(unittest.TestCase):
    def test_trigger_tiles_map_to_documented_transitions(self):
        tables = RomTables.from_rom(potos_rom())
        exit_ = tables.trigger_exit(130, 0xC0)
        self.assertEqual((exit_["map_id"], exit_["x"], exit_["y"], exit_["layer"]),
                         (129, 18, 61, 1))
        self.assertEqual(tables.trigger_exit(130, 0xC1)["map_id"], 131)
        # Scripts (< 0x800), ids below the base and beyond the list: unknown.
        self.assertIsNone(tables.trigger_exit(131, 0xC1))
        self.assertIsNone(tables.trigger_exit(131, 0xBF))
        self.assertIsNone(tables.trigger_exit(131, 0xC5))
        self.assertEqual(tables.neighbours(130), {129, 131})

    def test_copier_header_is_skipped(self):
        tables = RomTables.from_rom(potos_rom()[:0] + make_rom({130: [0x801]}, header=512))
        self.assertEqual(tables.trigger_exit(130, 0xC0)["map_id"], 129)

    def test_relocated_event_list_leaves_that_map_without_exits(self):
        tables = RomTables.from_rom(make_rom({50: [0x801], 130: [0x801]}, relocate=51))
        self.assertNotIn(51, tables.maps)
        self.assertEqual(tables.trigger_exit(130, 0xC0)["map_id"], 129)

    def test_foreign_layout_is_rejected(self):
        with self.assertRaises(RomLayoutError):
            RomTables.from_rom(bytes(range(256)) * 0x1000)

    def test_cache_round_trip_skips_refetch_when_digest_matches(self):
        rom = potos_rom()
        with tempfile.TemporaryDirectory() as directory:
            cache = Path(directory) / "exits.json"
            first = load_tables(lambda: rom, cache, lambda: None)
            fetched = []
            again = load_tables(lambda: fetched.append(1) or rom, cache,
                                lambda: first.digest)
            self.assertEqual(fetched, [])
            self.assertEqual(again.trigger_exit(130, 0xC0), first.trigger_exit(130, 0xC0))

    def test_scan_finds_exit_tiles_in_the_live_work_map(self):
        tables = RomTables.from_rom(potos_rom())
        wram = wram_with_tiles(40, 48, {(33, 11): 0xC0, (33, 31): 0xC1, (5, 5): 0x20})
        exits = scan_exit_tiles(wram, 130, tables)
        self.assertEqual(sorted((e["x"], e["y"], e["to_map"]) for e in exits),
                         [(33, 11, 129), (33, 31, 131)])


class TestMapLoadGuard(unittest.TestCase):
    @staticmethod
    def sample(map_id, size, mode="field"):
        return {"player_position": {}, "tile_context": {
            "map_id": map_id, "game_mode": mode, "runtime_verified": mode == "field",
            "map_size_tiles": size}}

    def test_stale_snapshots_after_a_map_change_are_not_trusted(self):
        guard = MapLoadGuard()
        self.assertTrue(guard.apply(self.sample(130, [40, 48]))["tile_context"]
                        ["runtime_verified"])
        # Live 130 -> 131: id changes, size and tiles are still map 130's.
        stale = guard.apply(self.sample(131, [40, 48]))
        self.assertFalse(stale["tile_context"]["runtime_verified"])
        self.assertEqual(stale["tile_context"]["verification_error"], "map_loading")
        self.assertIsNone(stale["player_position"])
        # The size changes once map 131 is loaded.
        self.assertTrue(guard.apply(self.sample(131, [52, 52]))["tile_context"]
                        ["runtime_verified"])

    def test_loader_mode_or_timeout_ends_the_wait(self):
        guard = MapLoadGuard(settle=3)
        guard.apply(self.sample(130, [40, 48]))
        guard.apply(self.sample(129, [40, 48]))
        guard.apply(self.sample(129, None, "transition"))
        self.assertTrue(guard.apply(self.sample(129, [40, 48]))["tile_context"]
                        ["runtime_verified"])
        # Same size and no loader sample seen: trusted after `settle` samples
        # (the sample with the new id counts).
        guard.apply(self.sample(128, [40, 48]))
        results = [guard.apply(self.sample(128, [40, 48]))["tile_context"]
                   ["runtime_verified"] for _ in range(3)]
        self.assertEqual(results, [False, False, True])

    def test_loading_samples_are_not_walked(self):
        with tempfile.TemporaryDirectory() as directory:
            history = ObservationHistory(Path(directory) / "walk.sqlite")
            state = self.sample(131, [40, 48])
            state["tile_context"].update(actor_tile=[3, 4], map_loading=True)
            self.assertFalse(history.remember_walk(state))
            history.close()


class TestRomCrossChecks(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.history = ObservationHistory(Path(self.directory.name) / "rom.sqlite")
        self.history.rom_tables = RomTables.from_rom(potos_rom())

    def tearDown(self):
        self.history.close()
        self.directory.cleanup()

    def cross(self, from_map, to_map, trigger_offset, trigger_id=0xC0):
        self.history.record(crossing_state(from_map, (32, 11), trigger_offset, trigger_id))
        self.history.record(crossing_state(to_map, (15, 61)))

    def test_untrusted_until_a_crossing_confirms_the_table(self):
        self.assertFalse(self.history.rom_exits_trusted())
        self.cross(130, 129, (1, 0))
        self.assertTrue(self.history.rom_exits_trusted())

    def test_one_contradiction_disables_rom_exits(self):
        self.cross(130, 129, (1, 0))
        # Trigger 0xC0 of map 130 predicts 129, but the hero entered 128.
        self.history.record(crossing_state(129, (2, 2)))
        self.cross(130, 128, (0, 1))
        self.assertFalse(self.history.rom_exits_trusted())

    def test_crossing_without_a_rom_trigger_is_not_judged(self):
        self.cross(130, 257, None)
        self.assertEqual(tuple(self.history.rom_check_summary()), (0, 0))

    def test_multi_map_path_uses_trusted_rom_exits_beyond_learned_maps(self):
        self.cross(130, 129, (1, 0))
        # Only 130 <-> 129 was walked; 131 -> 130 -> 129 -> 128 comes from the ROM.
        self.assertEqual(self.history.map_path(131, 128), [131, 130, 129, 128])
        self.history.remember_exits(131, [
            {"layer": 1, "x": 4, "y": 23, "tile_id": 0xC0, "to_map": 130,
             "to_tile": [27, 31], "to_layer": 1}])
        waypoints = self.history.exit_waypoints(131, 128)
        self.assertEqual(waypoints[0]["target"], [4, 23])
        self.assertEqual(waypoints[0]["source"], "rom")

    def test_reset_shaped_shortcut_never_beats_a_verified_rom_route(self):
        self.cross(130, 129, (1, 0))
        self.history.connection.execute(
            "INSERT INTO map_edges VALUES(131,128,1,10,23,'wait',1,0)")
        self.history.connection.commit()
        self.assertEqual(self.history.map_path(131, 128), [131, 130, 129, 128])

    def test_ko_return_is_not_learned_as_an_exit(self):
        before = crossing_state(130, (15, 25))
        before["party"] = [{"hp": 20}]
        dead = dict(before, party=[{"hp": 0}])
        dead["tile_context"] = dict(before["tile_context"], runtime_verified=False)
        after = crossing_state(128, (31, 58))
        after["party"] = [{"hp": 98}]
        self.history.record(before, "step_right")
        self.history.record(dead, "wait")
        self.history.record(after, "step_left")
        self.assertIsNone(self.history.connection.execute(
            "SELECT 1 FROM map_edges WHERE from_map=130 AND to_map=128").fetchone())

    def test_attach_replays_log_without_stale_map_snapshots(self):
        self.history.rom_tables = None
        before = crossing_state(130, (32, 11), (1, 0))
        stale = crossing_state(129, (34, 11))        # still map 130's data
        stale["tile_context"]["map_size_tiles"] = [40, 48]
        loaded = crossing_state(129, (15, 61))
        loaded["tile_context"]["map_size_tiles"] = [64, 72]
        for state in (before, stale, loaded):
            self.history.record(state)
        self.history.attach_rom(RomTables.from_rom(potos_rom()))
        row = self.history.connection.execute(
            "SELECT x,y FROM map_edges WHERE from_map=129 AND to_map=130").fetchone()
        self.assertEqual(row, (15, 61))
        self.assertTrue(self.history.rom_exits_trusted())


class TestMultiMapTravel(unittest.TestCase):
    def test_planner_walks_onto_the_rom_exit_tile_toward_a_far_map(self):
        with tempfile.TemporaryDirectory() as directory:
            history = ObservationHistory(Path(directory) / "travel.sqlite")
            history.rom_tables = RomTables.from_rom(potos_rom())
            history.record(crossing_state(130, (32, 11), (1, 0)))
            history.record(crossing_state(129, (15, 61)))
            history.remember_exits(131, [
                {"layer": 1, "x": 0, "y": 3, "tile_id": 0xC0, "to_map": 130,
                 "to_tile": [27, 31], "to_layer": 1}])
            for x in range(8):
                for y in range(8):
                    history.connection.execute(
                        "INSERT OR REPLACE INTO world_cells VALUES(131,1,?,?,'open',1,0)",
                        (x, y))
            state = {"weapons_enabled": True, "enemy_slots": [],
                     "party": [{"hp": 80, "max_hp": 100, "weapon_recharge": 0}],
                     "tile_context": {
                         "runtime_verified": True, "game_mode": "field", "radius": 1,
                         "map_id": 131, "map_size_tiles": [8, 8], "actor_tile": [2, 3],
                         "actor": {"x_px": 40, "y_px": 56, "layer": 1},
                         "passability_rows": ["...", ".@.", "..."]}}
            errand = {"place": "waterfall", "anchor": {"map_id": 128, "layer": 1,
                                                       "target": [30, 30]}}
            decision = MemoryPlanner(history).decide(state, errand=errand)
            self.assertEqual(decision.action, "step_left")
            self.assertIn("exit to map 130 via maps 131>130>129>128", decision.reason)
            state["tile_context"]["actor_tile"] = [0, 3]
            decision = MemoryPlanner(history).decide(state, errand=errand)
            self.assertEqual((decision.action, decision.reason), ("wait", "on exit to map 130"))
            history.close()

    def test_reachable_rom_exit_beats_a_nearer_learned_crossing(self):
        planner = MemoryPlanner()
        for x in range(8):
            for y in range(8):
                planner.local_cells[(131, 1, x, y)] = "open"
        context = {"map_id": 131, "map_size_tiles": [8, 8], "actor_tile": [2, 3],
                   "actor": {"layer": 1, "floor_height": 0}}
        learned = {"map_id": 131, "layer": 1, "target": [3, 3], "action": "step_right",
                   "source": "learned"}
        rom = {"map_id": 131, "layer": 1, "target": [6, 3], "action": None, "source": "rom"}
        other_layer = {"map_id": 131, "layer": 2, "target": [2, 4], "action": None,
                       "source": "rom"}
        self.assertIs(planner._pick_waypoint(context, [other_layer, learned, rom]), rom)
        # Unreachable ROM exit: the learned crossing is still used.
        for y in range(8):
            planner.local_cells[(131, 1, 5, y)] = "blocked"   # maps wrap: wall
            planner.local_cells[(131, 1, 7, y)] = "blocked"   # both sides
        self.assertIs(planner._pick_waypoint(context, [rom, learned]), learned)


if __name__ == "__main__":
    unittest.main()

import unittest

import som_npc_native as native
from som_memory import WRAM

FLOOR, WALL, DOOR = 0x10, 0x20, 0xC4


def synthetic(npc_tile=(29, 25), hero_tile=(10, 10), walls=(), static=True, slot=8,
              kind=0xC7, record=8, door=None):
    """Field-mode Potos-like WRAM: floor-2 ground, optional walls/door."""
    data = bytearray(0x20000)

    def put16(address, value):
        data[address - 0x7E0000] = value & 0xFF
        data[address - 0x7E0000 + 1] = value >> 8

    data[0xE000] = 1
    data[0xE4], data[0xE6] = 1, 0x0F
    put16(0x7E00DC, 131)
    put16(0x7E00C0, 832)
    put16(0x7E00C2, 832)
    for tile_id, attributes in ((FLOOR, (18, 6, 0, 0)), (WALL, (19, 6, 0, 0)),
                                (DOOR, (16, 128, 0, 0))):
        for i, value in enumerate(attributes):
            data[0x7FB800 - 0x7E0000 + tile_id * 4 + i] = value
    for y in range(52):
        for x in range(52):
            data[0x7F0000 - 0x7E0000 + y * 128 + x] = FLOOR
    for x, y in walls:
        data[0x7F0000 - 0x7E0000 + y * 128 + x] = WALL
    if door:
        data[0x7F0000 - 0x7E0000 + door[1] * 128 + door[0]] = DOOR
    put16(0x7EE002, hero_tile[0] * 16 + 8)
    put16(0x7EE004, hero_tile[1] * 16 + 4)
    data[0xE00B] = 2
    base = native.object_base(slot)
    offset = base - 0x7E0000
    data[offset] = 1
    data[offset + 0x3D] = kind
    data[offset + 0x44] = 0xC0 if static else 0x40
    data[offset + 0x0B] = 2
    data[offset + 0x71] = 0x0A
    put16(base + 0x34, record * 16)
    x, y = native.record_pixel(*npc_tile)
    put16(base + 2, x)
    put16(base + 4, y)
    return data


class NativeTest(unittest.TestCase):
    def test_decode_and_bind_by_kind_and_record(self):
        wram = WRAM(bytes(synthetic()))
        npcs = native.decode_npcs(wram)
        self.assertEqual(len(npcs), 1)
        live = npcs[0]
        self.assertEqual((live["slot"], live["kind"], live["record"], live["tile"]),
                         (8, 0xC7, 8, (29, 25)))
        self.assertTrue(live["static"])
        self.assertIs(native.bind(npcs, {"kind": 0xC7, "record": 8}), live)
        self.assertIsNone(native.bind(npcs, {"kind": 0xC7, "record": 7}))
        self.assertIsNone(native.bind(npcs, {"kind": 0xC5, "record": 8}))

    def test_heroes_and_monsters_are_never_npcs(self):
        data = synthetic(kind=0x05)
        self.assertEqual(native.decode_npcs(WRAM(bytes(data))), [])
        with self.assertRaises(ValueError):
            native.actor_walk_writes(8, 4, 8, 0)

    def test_refusal_mirrors_proxy_gates(self):
        data = synthetic()
        wram = WRAM(bytes(data))
        args = (8, 0xC7, 472, 404, 131)
        self.assertIsNone(native.walk_refusal(wram, *args))
        self.assertEqual(native.walk_refusal(wram, 2, 0xC7, 472, 404, 131),
                         "slot is not an NPC slot")
        self.assertEqual(native.walk_refusal(wram, 8, 0xC7, 470, 404, 131), "stale position")
        self.assertEqual(native.walk_refusal(wram, 8, 0xC7, 472, 404, 130), "different map")
        data[0xD0] = 0x81
        self.assertEqual(native.walk_refusal(WRAM(bytes(data)), *args), "event running")
        data[0xD0] = 0
        data[0x3A], data[0x3B] = 0x00, 0xF0
        self.assertEqual(native.walk_refusal(WRAM(bytes(data)), *args), "NPC is the talk target")
        wandering = WRAM(bytes(synthetic(static=False)))
        self.assertEqual(native.walk_refusal(wandering, *args), "NPC has native AI (not static)")

    def test_actor_walk_write_set_matches_rom_routine(self):
        writes = dict(native.actor_walk_writes(8, 3, 16, 0x0A))
        base = native.object_base(8)
        self.assertEqual(writes[base + 0x06], 0x82)
        self.assertEqual(writes[base + 0x07], 0x00)
        self.assertEqual(writes[base + 0x10], 0x82)
        self.assertEqual(writes[base + 0x0A], 16)
        self.assertEqual(writes[base + 0x42], 1)
        self.assertEqual(writes[base + 0x30], 0xFF)
        self.assertEqual(writes[base + 0x11], 1)
        self.assertEqual(len(writes), 10)
        hidden = dict(native.actor_walk_writes(8, 1, 4, 0x40))
        self.assertEqual((hidden[base + 0x10], hidden[base + 0x11]), (0, 0))

    def test_command_format_is_numeric(self):
        live = native.decode_npcs(WRAM(bytes(synthetic())))[0]
        self.assertEqual(native.walk_command(131, live, "left", 16),
                         "npc_walk 131 8 199 472 404 3 16")
        with self.assertRaises(ValueError):
            native.walk_command(131, live, "left", 33)

    def test_route_avoids_walls_doors_and_occupied_tiles(self):
        walls = [(28, y) for y in range(20, 29)]
        wram = WRAM(bytes(synthetic(walls=walls, door=(29, 27))))
        grid = native.walkable_grid(wram, 1, 2, (20, 18, 36, 34))
        self.assertFalse(grid[(28, 25)])
        self.assertFalse(grid[(29, 27)])          # trigger tile (door) is never entered
        route = native.plan_route(grid, (29, 25), (26, 25), occupied={(30, 29)})
        self.assertIsNotNone(route)
        position = (29, 25)
        for step in route:
            dx, dy = native.DIRECTIONS[step][1]
            position = (position[0] + dx, position[1] + dy)
            self.assertTrue(grid[position])
            self.assertNotEqual(position, (30, 29))
        self.assertEqual(position, (26, 25))
        self.assertEqual(native.plan_route(grid, (29, 25), (29, 25)), [])
        self.assertIsNone(native.plan_route(grid, (29, 25), (28, 25)))

    def test_first_run_and_frames(self):
        self.assertEqual(native.first_run(["down", "down", "left"]), ("down", 2))
        self.assertEqual(native.first_run(["left"] * 9), ("left", 4))
        self.assertIsNone(native.first_run([]))
        self.assertEqual(native.frames_for_tiles(1), 8)
        self.assertEqual(native.frames_for_tiles(10), native.MAX_WALK_FRAMES)

    def test_verify_rom_rejects_other_roms(self):
        results = native.verify_rom(bytes(0x400000))
        self.assertEqual(len(results), len(native.ROM_FINGERPRINTS))
        self.assertFalse(any(ok for _, ok in results))


if __name__ == "__main__":
    unittest.main()

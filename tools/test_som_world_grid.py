import unittest
from types import SimpleNamespace

from som_world_grid import CROSSING, WorldGrid, compass


def tables(links):
    """links: (from, to, arrival x, arrival y, code) -> a RomTables look-alike."""
    maps, transitions = {}, []
    for source, target, x, y, code in links:
        maps.setdefault(source, {"base": 192, "entry": False, "events": []})
        maps.setdefault(target, {"base": 192, "entry": False, "events": []})
        maps[source]["events"].append(0x800 + len(transitions))
        transitions.append((target, x, y, 0, 0x80 | code))

    def neighbours(map_id):
        return {transitions[e - 0x800][0] for e in maps.get(map_id, {}).get("events", ())}
    return SimpleNamespace(maps=maps, transitions=transitions, neighbours=neighbours)


# A field (1) with a town to the north (2) by a walk up, a hut (3) entered
# from the field, and a long field (4) to the east by a walk right.
WORLD = tables([
    (1, 2, 10, 30, 1), (2, 1, 10, 1, 2),     # walk up / walk back down
    (1, 3, 5, 9, 5), (3, 1, 20, 20, 0),      # into the hut / out of it
    (1, 4, 1, 10, 3), (4, 1, 30, 10, 4),     # walk right / walk back left
    (4, 5, 2, 2, 3), (5, 4, 90, 10, 4),
    (2, 5, 2, 30, 3), (5, 2, 1, 2, 4),       # a short way from the town
])


class WorldGridTest(unittest.TestCase):
    def setUp(self):
        self.grid = WorldGrid.from_rom(WORLD)

    def test_walk_links_place_maps_edge_to_edge(self):
        # The town's arrival tile (10, 30) sits one step north of the field's
        # door to it (10, 1): the town is north of the field.
        self.assertEqual(self.grid.point(2, (10, 30)), self.grid.point(1, (10, 0)))
        self.assertEqual(self.grid.heading(1, 2, (10, 1)), "north")
        self.assertEqual(self.grid.heading(1, 4, (30, 10)), "east")
        self.assertTrue(self.grid.toward(1, 2, "north"))
        self.assertFalse(self.grid.toward(1, 4, "north"))

    def test_houses_are_rooms_at_their_door(self):
        self.assertIn(3, self.grid.room)
        self.assertNotIn(2, self.grid.room)
        self.assertEqual(self.grid.island[3], self.grid.island[1])

    def test_route_is_measured_in_walked_tiles_not_hops(self):
        maps, tiles = self.grid.route(1, 5, WORLD.neighbours, start_tile=(10, 2))
        self.assertEqual(maps, [1, 2, 5])
        # 1 tile to the field's door north, 9 + 28 across the town to its
        # door to 5; the way through map 4 walks 28 + 89.
        self.assertEqual(tiles, 1 + CROSSING + (9 + 28) + CROSSING)
        far = self.grid.distances(1, WORLD.neighbours, start_tile=(10, 2))
        self.assertEqual(far[5], tiles)
        self.assertIsNone(self.grid.route(1, 99, WORLD.neighbours))

    def test_separate_islands_have_no_heading(self):
        grid = WorldGrid.from_rom(tables([(1, 2, 1, 1, 1), (7, 8, 1, 1, 1)]))
        self.assertIsNone(grid.heading(1, 7))
        self.assertIsNone(grid.span(1, 8))
        self.assertEqual(grid.describe(1, 7), "")

    def test_compass_words(self):
        self.assertEqual(compass(0, -5), "north")
        self.assertEqual(compass(5, 5), "south-east")
        self.assertEqual(compass(-5, 0), "west")


if __name__ == "__main__":
    unittest.main()

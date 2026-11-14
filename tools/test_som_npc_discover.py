import json
import unittest

import som_npc_discover as discover
from som_memory import WRAM
from som_npc_schedule import DEFAULT_PATH, World
from test_som_npc_native import synthetic


class DiscoverTest(unittest.TestCase):
    def setUp(self):
        self.schedule = json.loads(DEFAULT_PATH.read_text())

    def test_plans_a_reachable_life_for_a_new_static_villager(self):
        wram = WRAM(bytes(synthetic(npc_tile=(20, 20), record=12, kind=0xC3)))
        locations, npcs = discover.plan_villagers(wram, self.schedule)
        self.assertEqual([n["native"]["record"] for n in npcs], [12])
        npc = npcs[0]
        home, work, meet = (locations[f"potos_r12_{k}"] for k in ("home", "work", "meet"))
        self.assertEqual((home["x"], home["y"]), (20, 20))
        self.assertNotEqual((work["x"], work["y"]), (meet["x"], meet["y"]))
        for spot in (work, meet):
            self.assertLessEqual(abs(spot["x"] - 20) + abs(spot["y"] - 20), discover.MAX_ROUTE)
        World(discover.merge(self.schedule, locations, npcs))      # validates

    def test_skips_scheduled_and_moving_villagers(self):
        wram = WRAM(bytes(synthetic(record=8)))                    # already scheduled
        self.assertEqual(discover.plan_villagers(wram, self.schedule)[1], [])
        wram = WRAM(bytes(synthetic(npc_tile=(20, 20), record=12, static=False)))
        self.assertEqual(discover.plan_villagers(wram, self.schedule)[1], [])

    def test_routines_are_offset_so_the_town_does_not_move_in_step(self):
        self.assertEqual(discover.shift("23:50", 20), "00:10")
        a = WRAM(bytes(synthetic(npc_tile=(20, 20), record=12)))
        b = WRAM(bytes(synthetic(npc_tile=(20, 20), record=13)))
        first = discover.plan_villagers(a, self.schedule)[1][0]["schedule"][0]["time"]
        second = discover.plan_villagers(b, self.schedule)[1][0]["schedule"][0]["time"]
        self.assertNotEqual(first, second)


if __name__ == "__main__":
    unittest.main()

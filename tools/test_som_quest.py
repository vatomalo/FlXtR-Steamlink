import unittest

import som_quest as Q


class FakeHistory:
    def __init__(self):
        self.state = {}

    def get_agent_state(self, key):
        return self.state.get(key)

    def set_agent_state(self, key, value):
        self.state[key] = value


class FakeWram:
    def __init__(self, flags):
        self.flags = flags

    def u8(self, address):
        return self.flags.get(address - Q.FLAG_BASE, 0)


DATA = {
    "bosses": [
        {"map": 260, "id": 0x57, "flag": 0x10, "min": 0, "max": 15, "x": 1, "y": 1, "level": 3},
        {"map": 290, "id": 0x59, "flag": 0x14, "min": 0, "max": 3, "x": 1, "y": 1, "level": 7},
        {"map": 310, "id": 0x5B, "flag": 0x17, "min": 5, "max": 5, "x": 1, "y": 1, "level": 10},
        {"map": 400, "id": 0x60, "flag": 0x28, "min": 0, "max": 15, "x": 1, "y": 1, "level": 23},
    ],
    "monsters": {"16": [1, 2, 3, 5], "23": [1, 2, 4], "30": [8, 8, 9, 10], "31": [7, 7, 8], "50": [20, 21]},
}


def summary(level, map_id=16):
    return {"level": level - 1, "map_id": map_id}      # +0x181 is 0-based


class QuestTest(unittest.TestCase):
    def quest(self, flags=None, weights=(3, 3)):
        quest = Q.Quest(DATA, FakeHistory(), log=lambda line: None)
        quest.update(wram=FakeWram(flags or {}), weights=weights)
        return quest

    def test_lowest_level_boss_not_behind_is_the_story(self):
        # Level 10: the level-3 boss is 6+ below, so behind; next is level 7.
        self.assertEqual(self.quest().choose(summary(10), lambda m: True)[0], 290)

    def test_flag_past_its_range_means_beaten(self):
        quest = self.quest({0x14: 4})                   # 0x59 needs 0-3
        boss = quest.story_boss(10, lambda m: True)
        self.assertEqual(boss["id"], 0x60)              # 0x5B waits for flag 5

    def test_a_seen_kill_is_remembered(self):
        quest = self.quest()
        quest.update(observation={"tile_context": {"map_id": 290}, "enemy_slots": [
            {"slot": 0, "identity": 0x59, "active": True}]})
        quest.update(observation={"tile_context": {"map_id": 290}, "enemy_slots": [
            {"slot": 0, "identity": Q.DYING, "active": True}]})
        self.assertIn("89@290", quest.defeated())
        self.assertEqual(quest.story_boss(10, lambda m: True)["id"], 0x60)

    def test_level_gate_grinds_first(self):
        # Hero 5 vs boss level 7 + margin 1 (weight 3): grind where monsters
        # sit just above level 5.
        quest = self.quest()
        quest.history.state["bosses_defeated"] = ["87@260"]     # the Potos boss
        target, why = quest.choose(summary(5), lambda m: True)
        self.assertEqual(target, 31)
        self.assertIn("level up first", why)
        quest.update(weights=(3, 0))                    # no levelling: straight to the boss
        self.assertEqual(quest.choose(summary(5), lambda m: True)[0], 290)

    def test_unreachable_boss_is_skipped_and_nothing_left_explores(self):
        quest = self.quest()
        self.assertEqual(quest.choose(summary(10), lambda m: m == 400)[0], 400)
        self.assertIsNone(quest.choose(summary(10), lambda m: False))

    def test_a_boss_room_behind_a_story_event_sends_him_next_to_it(self):
        # 290 is entered only by scripts from 287 and 292; 287 is nearer.
        class Tables:
            @staticmethod
            def scripted_into(target, flags=None):
                return {287, 292} if target == 290 else set()
        quest = self.quest()
        quest.history.rom_tables = Tables()
        quest.history.map_path = lambda a, b: list(range(a, a + (3 if b == 287 else 5)))
        reachable = lambda m: m in (287, 292, 400)
        target, why = quest.choose(summary(10, 131), reachable)
        self.assertEqual(target, 287)
        self.assertIn("story event from map 287", why)
        # No door reachable: the next boss that is.
        self.assertEqual(quest.choose(summary(10, 131), lambda m: m == 400)[0], 400)

    def test_failed_story_trips_raise_a_level_floor_and_level_up_first(self):
        quest = self.quest(weights=(5, 0))
        logs = []
        quest.log = logs.append
        reachable = lambda m: True
        self.assertEqual(quest.choose(summary(11, 131), reachable)[0], 290)
        quest.story_outcome("retreated", 10, 16)             # raw level 10 = level 11
        self.assertEqual(quest.choose(summary(11, 131), reachable)[0], 290)   # one is chance
        quest.story_outcome("died", 10, 16)
        target, why = quest.choose(summary(11, 131), reachable)
        self.assertEqual(target, 16)
        self.assertIn("level up first", why)
        self.assertTrue(any("adjusting" in line and "level up to 12" in line for line in logs))
        # Reaching the floor sends him back to the story; failing again raises it.
        self.assertEqual(quest.choose(summary(12, 131), reachable)[0], 290)
        self.assertTrue(any("back to the story" in line for line in logs))
        quest.story_outcome("retreated", 11, 23)
        quest.story_outcome("retreated", 11, 23)
        self.assertEqual(quest.choose(summary(12, 131), reachable)[0], 23)
        self.assertEqual(quest.adjustment()["floor"], 13)
        # A failure noticed in a map without monsters (the inn house) is
        # never a place to level up: the last field map stays the target.
        quest.story_outcome("died", 11, 259)
        quest.story_outcome("died", 11, 259)
        self.assertEqual(quest.adjustment()["grind_map"], 23)
        self.assertEqual(quest.choose(summary(12, 131), reachable)[0], 23)
        # An arrival clears the count (the floor stays until it is reached).
        quest.story_outcome("retreated", 11, 23)
        quest.story_outcome("arrived", 11, 287)
        self.assertEqual(quest.adjustment()["failures"], 0)

    def test_both_weights_off_leaves_it_to_exploring(self):
        self.assertIsNone(self.quest(weights=(0, 0)).choose(summary(10), lambda m: True))

    def test_build_reads_boss_records_and_levels(self):
        rom = bytearray(0x101C00 + 0x80 * Q.MONSTER_SIZE)
        def word(offset, value):
            rom[offset:offset + 2] = value.to_bytes(2, "little")
        # map 0: header + one boss record + one monster record; map 1 ends it
        word(Q.MAP_POINTERS, 0x1000)
        word(Q.MAP_POINTERS + 2, 0x1000 + Q.HEADER + 2 * Q.RECORD)
        for m in range(2, Q.MAP_COUNT + 1):
            word(Q.MAP_POINTERS + 2 * m, 0x1000 + Q.HEADER + 2 * Q.RECORD)
        record = 0x081000 + Q.HEADER
        rom[record:record + 8] = bytes([0x14, 0x03, 5, 6, 0, 0x59, 0, 0])
        rom[record + 8:record + 16] = bytes([0x10, 0x0F, 2, 2, 0, 0x01, 0, 0])
        rom[Q.MONSTER_RECORDS + 0x59 * Q.MONSTER_SIZE] = 7
        rom[Q.MONSTER_RECORDS + 0x01 * Q.MONSTER_SIZE] = 4
        data = Q.build(bytes(rom))
        self.assertEqual(data["bosses"], [{"map": 0, "id": 0x59, "flag": 0x14, "min": 0,
                                           "max": 3, "x": 5, "y": 6, "level": 7}])
        self.assertEqual(data["monsters"], {"0": [4]})


if __name__ == "__main__":
    unittest.main()

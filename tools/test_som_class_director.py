"""Class choice: fit by build and weapon levels, sword for grass, calm switches."""
import unittest

from som_class_director import ClassDirector, SWORD_CLASSES, WEAPONS, score


class Clock:
    def __init__(self):
        self.now = 0.0

    def __call__(self):
        return self.now


class ClassDirectorTest(unittest.TestCase):
    def setUp(self):
        self.clock = Clock()
        self.director = ClassDirector(self.clock)

    def test_first_choice_follows_build_and_levels(self):
        index, _ = self.director.choose("skirmisher", {"sword": 1}, 0)
        self.assertIn(index, (2, 9))                 # Ninjutsu / Roguecraft
        index, _ = self.director.choose("brawler", {"glove": 3, "spear": 3}, 0)
        self.assertEqual(index, 1)                   # trained monk weapons win

    def test_magic_counts_only_with_mp(self):
        no_mp = self.director.choose("caster", {}, 0)[0]
        with_mp = self.director.choose("caster", {}, 20)[0]
        self.assertEqual(with_mp, 3)                 # Magic
        self.assertGreaterEqual(score(3, "caster", {}, 20), score(no_mp, "caster", {}, 20))

    def test_grass_forces_a_sword_class_even_in_cooldown(self):
        self.director.switched(1)                    # Martial Arts: glove
        index, reason = self.director.choose("brawler", {"glove": 5}, 0, need_cut=True)
        self.assertIn(index, SWORD_CLASSES)
        self.assertEqual(WEAPONS[index][0], "sword")
        self.assertIn("sword", reason)
        self.director.switched(index)                # already a sword: stay
        self.assertIsNone(self.director.choose("brawler", {"glove": 5}, 0, need_cut=True))

    def test_situation_advantages(self):
        index, why = self.director.choose("balanced", {}, 20, hp_fraction=0.3)
        self.assertEqual(index, 4)                    # Divine Arts heals
        self.assertIn("Cure Water", why)
        index, why = ClassDirector(self.clock).choose("balanced", {}, 0, enemies=1, nearest=90)
        self.assertEqual(index, 11)                   # Archery for range
        index, _ = ClassDirector(self.clock).choose("balanced", {}, 0, enemies=1, nearest=30,
                                                    toughest=400)
        self.assertEqual(index, 10)                   # Bushido vs a tough enemy

    def test_gambit_cooldown(self):
        self.director.switched(0)
        self.director.cooldown = None                 # "Switch classes" 0
        self.clock.now = 1000
        self.assertIsNone(self.director.choose("skirmisher", {}, 0))
        self.director.cooldown = 30
        self.assertIsNotNone(self.director.choose("skirmisher", {}, 0))

    def test_a_class_is_kept_at_least_min_hold(self):
        from som_class_director import MIN_HOLD_S
        self.director.cooldown = 10
        self.director.switched(2)
        self.clock.now += MIN_HOLD_S - 1
        self.assertIsNone(self.director.choose("skirmisher", {"bow": 9}, 0))
        self.clock.now += 2
        self.assertEqual(self.director.choose("skirmisher", {"bow": 9}, 0)[0], 11)

    def test_no_flip_flopping(self):
        self.director.switched(0)
        self.assertIsNone(self.director.choose("skirmisher", {}, 0))  # cooldown
        self.clock.now = 120
        self.assertIsNone(self.director.choose("skirmisher", {}, 0, in_combat=True))
        self.assertIsNotNone(self.director.choose("skirmisher", {}, 0))
        self.director.switched(2)
        self.clock.now = 400                         # a tie is not enough
        self.assertIsNone(self.director.choose("skirmisher", {}, 0))
        self.assertIsNone(self.director.choose("skirmisher", {"bow": 1}, 0))  # +1 is not
        self.assertEqual(self.director.choose("skirmisher", {"bow": 3}, 0)[0], 11)  # +3 is (Archery)


if __name__ == "__main__":
    unittest.main()


class WantedBuildTest(unittest.TestCase):
    """The wanted build: AI plan from stats and record, training rotation."""

    def setUp(self):
        self.clock = Clock()
        self.director = ClassDirector(self.clock)

    def test_player_names_classes_by_name_prefix_or_number(self):
        from som_class_director import parse_classes
        self.assertEqual(parse_classes("Bushido, arch, 2"), [10, 11, 2])
        with self.assertRaises(ValueError):
            parse_classes("S")                       # Swordsmanship or Summoning?

    def test_ai_plan_follows_the_record_once_classes_are_tried(self):
        from som_class_director import plan_classes
        untried, _ = plan_classes("balanced")
        self.assertIn(8, untried)                    # stat fit leads at first
        record = {8: (200, 100, 200), 0: (200, 900, 50), 10: (200, 800, 40)}
        learned, why = plan_classes("balanced", record)
        self.assertNotIn(8, learned)                 # Weapon Flow lost HP badly
        self.assertIn(0, learned)
        self.assertIn("Swordsmanship", why)

    def test_healers_need_mp_for_the_plan(self):
        from som_class_director import plan_classes
        self.assertNotIn(4, plan_classes("guardian", {4: (100, 900, 0)}, max_mp=0)[0])

    def test_calm_rotates_to_the_least_trained_wanted_class(self):
        self.director.set_plan([8, 10, 11])
        self.director.switched(8)
        self.clock.now = 1000
        levels = {"sword": 3, "spear": 3, "bow": 0, "javelin": 0}
        index, why = self.director.choose("balanced", levels, 0)
        self.assertEqual(index, 11)                  # Archery: bow and javelin untrained
        self.assertIn("training", why)
        self.director.switched(11)
        self.clock.now = 2000
        levels = {"sword": 3, "spear": 3, "bow": 1, "javelin": 1}
        self.assertIsNone(self.director.choose("balanced", levels, 0))   # keeps training

    def test_calm_does_not_drift_back_to_the_default_class(self):
        self.director.set_plan([10, 11, 2])
        self.director.switched(10)
        self.clock.now = 1000
        self.assertIsNone(self.director.choose("balanced", {"sword": 2, "bow": 2,
                                                            "boomerang": 2, "javelin": 2}, 0))

    def test_fights_favour_wanted_classes_but_strong_needs_still_win(self):
        self.director.set_plan([0, 10, 11])
        index, _ = self.director.choose("balanced", {}, 0, enemies=1, nearest=30)
        self.assertIn(index, (0, 10, 11))
        index, _ = ClassDirector(self.clock).choose("balanced", {}, 20, hp_fraction=0.2,
                                                    enemies=1, nearest=30)
        self.assertEqual(index, 4)                   # Cure Water when hurt

    def test_record_counts_damage_per_class(self):
        import sqlite3
        from som_class_director import ClassRecord
        record = ClassRecord(sqlite3.connect(":memory:"))
        def state(hero_hp, enemy_hp):
            return {"party": [{"hp": hero_hp, "max_hp": 100}],
                    "tile_context": {"map_id": 1, "actor": {"x_px": 0, "y_px": 0}},
                    "enemy_slots": [{"slot": 1, "active": True, "identity": 7,
                                     "x_px": 20, "y_px": 0, "hp": enemy_hp}]}
        record.record(state(80, 50), 10)
        record.record(state(75, 30), 10)
        self.assertEqual(record.stats(), {10: (1, 20, 5)})


class TeamPlanTest(unittest.TestCase):
    def test_archetype_names_expand_to_their_team(self):
        from som_class_director import parse_classes
        self.assertEqual(sorted(parse_classes("shinobi")), [1, 2, 10])
        self.assertEqual(parse_classes("ranger, Bushido"), [11, 9, 2, 10])

    def test_plan_is_a_team_that_covers_the_fights(self):
        from som_class_director import plan_classes, team_score
        wanted, why = plan_classes("balanced")
        self.assertEqual(len(wanted), 3)
        self.assertIn("covers", why)
        _, covered = team_score(wanted, {i: 0 for i in range(12)}, False)
        self.assertGreaterEqual(len(covered), 3)

    def test_synergy_beats_three_strong_loners(self):
        from som_class_director import team_score
        values = {i: 0 for i in range(12)}
        shinobi, _ = team_score((1, 2, 10), values, False)
        loners, _ = team_score((3, 5, 6), values, False)       # spells, no MP
        self.assertGreater(shinobi, loners)

    def test_a_team_that_fights_well_together_wins(self):
        from som_class_director import plan_classes, team_key
        good = {team_key((1, 2, 10)): (300, 3000, 0)}
        wanted, why = plan_classes("balanced", team_record=good)
        self.assertEqual(wanted, [1, 2, 10])
        self.assertIn("shinobi", why)
        self.assertIn("together", why)

    def test_record_counts_team_fights_only_under_a_member(self):
        import sqlite3
        from som_class_director import ClassRecord
        record = ClassRecord(sqlite3.connect(":memory:"))
        def state(hero_hp, enemy_hp):
            return {"party": [{"hp": hero_hp, "max_hp": 100}],
                    "tile_context": {"map_id": 1, "actor": {"x_px": 0, "y_px": 0}},
                    "enemy_slots": [{"slot": 1, "active": True, "identity": 7,
                                     "x_px": 20, "y_px": 0, "hp": enemy_hp}]}
        record.record(state(80, 50), 10, (1, 2, 10))
        record.record(state(80, 40), 0, (1, 2, 10))           # Swordsmanship: not a member
        record.record(state(80, 30), 0, (1, 2, 10))
        self.assertEqual(record.team_stats(), {"1,2,10": (1, 10, 0)})
        self.assertEqual(record.stats()[0], (1, 10, 0))

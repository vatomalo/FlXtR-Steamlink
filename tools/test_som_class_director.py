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

    def test_no_flip_flopping(self):
        self.director.switched(0)
        self.assertIsNone(self.director.choose("skirmisher", {}, 0))  # cooldown
        self.clock.now = 120
        self.assertIsNone(self.director.choose("skirmisher", {}, 0, in_combat=True))
        self.assertIsNotNone(self.director.choose("skirmisher", {}, 0))
        self.director.switched(2)
        self.clock.now = 400                         # a tie is not enough
        self.assertIsNone(self.director.choose("skirmisher", {}, 0))
        self.assertEqual(self.director.choose("skirmisher", {"bow": 1}, 0)[0], 9)  # +1 is


if __name__ == "__main__":
    unittest.main()

"""Samurai Iaijutsu timing and the weaponsmaster's third weapon."""
import unittest

from som_class_arts import IAI_COOLDOWN_S, KICK_COOLDOWN_S, FlyingKickTactics, IaiTactics
from som_class_director import BUSHIDO, NO_MAGIC, THIRD_WEAPONS, ClassDirector
from som_weapon_tactics import choose


class Clock:
    def __init__(self):
        self.t = 10.0

    def __call__(self):
        return self.t


class FlyingKickTest(unittest.TestCase):
    def test_kicks_an_enemy_in_line_ahead(self):
        clock = Clock()
        kick = FlyingKickTactics(clock)
        self.assertIsNone(kick.decide("step_right", 100, 100, [(120, 100)]))   # glove reach
        self.assertIsNone(kick.decide("step_right", 100, 100, [(60, 100)]))    # behind
        self.assertIsNone(kick.decide("step_right", 100, 100, [(140, 130)]))   # off the line
        self.assertIsNone(kick.decide("step_right", 100, 100, [(200, 100)]))   # too far
        self.assertEqual(kick.decide("step_up", 100, 100, [(104, 60)]), "flykick")
        self.assertIsNone(kick.decide("step_up", 100, 100, [(104, 60)]))       # cooling down
        clock.t += KICK_COOLDOWN_S
        self.assertEqual(kick.decide("step_left", 100, 100, [(55, 108)]), "flykick")
        self.assertIsNone(kick.decide(None, 100, 100, [(140, 100)]))


class PlayerPickTest(unittest.TestCase):
    def test_a_class_picked_in_the_game_is_kept(self):
        clock = Clock()
        director = ClassDirector(clock)
        director.switched(9)                       # the AI picked Roguecraft
        self.assertFalse(director.adopt(9))
        self.assertFalse(director.adopt(1))        # the save file may still lag
        clock.t += 5
        self.assertTrue(director.adopt(1))         # the player picked Martial Arts
        self.assertEqual(director.current, 1)
        self.assertIsNone(director.choose("balanced", {}, 0, False, False,
                                          hp_fraction=1.0, enemies=0, nearest=None,
                                          toughest=0))
        self.assertFalse(director.adopt(None))


class IaiTest(unittest.TestCase):
    def test_draws_on_attack_with_cooldown_and_level(self):
        clock = Clock()
        iai = IaiTactics(clock)
        self.assertIsNone(iai.decide("step_left"))
        self.assertEqual(iai.decide("attack"), "iai_3")
        self.assertIsNone(iai.decide("attack"))              # cooling down
        clock.t += IAI_COOLDOWN_S
        self.assertEqual(iai.decide("attack", toughest_hp=400), "iai_4")
        clock.t += IAI_COOLDOWN_S
        self.assertEqual(iai.decide("attack", enemies_near=3), "iai_4")
        iai.refused()
        clock.t += 0.5
        self.assertEqual(iai.decide("attack"), "iai_3")

    def test_class_data(self):
        self.assertIn(BUSHIDO, NO_MAGIC)
        self.assertEqual(THIRD_WEAPONS[8], "bow")


class ThirdWeaponTest(unittest.TestCase):
    def test_bow_for_far_enemies_sword_or_spear_up_close(self):
        self.assertEqual(choose("sword", "spear", "sword", 60, 1.0, {}, "bow")[0], "bow")
        self.assertIn(choose("sword", "spear", "bow", 12, 1.0, {}, "bow")[0], ("sword", "spear"))
        self.assertEqual(choose("sword", "spear", "sword", 20, 1.0, {"spear": 3}, "bow")[0],
                         "spear")
        # No third weapon: the old pair rules.
        self.assertEqual(choose("sword", "spear", "sword", 60, 1.0, {})[0], "spear")
        # Training the weakest of three.
        self.assertEqual(choose("sword", "spear", "sword", None, 1.0,
                                {"sword": 4, "spear": 4, "bow": 1}, "bow")[0], "bow")


if __name__ == "__main__":
    unittest.main()

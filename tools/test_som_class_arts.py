"""Samurai Iaijutsu timing and the weaponsmaster's third weapon."""
import unittest

from som_class_arts import IAI_COOLDOWN_S, IaiTactics
from som_class_director import BUSHIDO, NO_MAGIC, THIRD_WEAPONS
from som_weapon_tactics import choose


class Clock:
    def __init__(self):
        self.t = 10.0

    def __call__(self):
        return self.t


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

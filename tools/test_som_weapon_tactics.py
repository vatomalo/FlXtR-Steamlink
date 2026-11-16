"""Weapon alternation: reach, low HP, whip blind spot, training, cooldown."""
import unittest

from som_weapon_tactics import WeaponTactics, choose


class ChooseTest(unittest.TestCase):
    def test_reach_and_close_range(self):
        self.assertEqual(choose("sword", "bow", "sword", 60, 1.0, {})[0], "bow")
        self.assertEqual(choose("sword", "bow", "bow", 20, 1.0, {"sword": 2})[0], "sword")
        weapon, why = choose("sword", "bow", "sword", 60, 1.0, {})
        self.assertIn("60 px", why)

    def test_whip_blind_spot_and_low_hp(self):
        self.assertEqual(choose("whip", "glove", "whip", 12, 1.0, {})[0], "glove")
        self.assertEqual(choose("sword", "bow", "sword", 20, 0.3, {})[0], "bow")

    def test_idle_training_and_ready(self):
        self.assertEqual(choose("sword", "axe", "sword", None, 1.0, {"sword": 3, "axe": 0})[0], "axe")
        self.assertEqual(choose("sword", "axe", "axe", None, 1.0, {"sword": 1, "axe": 1})[0], "sword")


class TacticsTest(unittest.TestCase):
    def test_cooldown_and_busy(self):
        now = [0.0]
        t = WeaponTactics(lambda: now[0])
        self.assertIsNone(t.decide("sword", "bow", "sword", 60, 1.0, {}, busy=True))
        self.assertEqual(t.decide("sword", "bow", "sword", 60, 1.0, {})[0], "bow")
        now[0] = 1.0
        self.assertIsNone(t.decide("sword", "bow", "bow", 20, 1.0, {}))   # cooldown
        now[0] = 5.0
        self.assertEqual(t.decide("sword", "bow", "bow", 20, 1.0, {})[0], "sword")


if __name__ == "__main__":
    unittest.main()

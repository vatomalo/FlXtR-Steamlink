"""Weapon alternation: reach, low HP, whip blind spot, training, cooldown."""
import unittest

from som_weapon_tactics import WeaponTactics, choose


class ChooseTest(unittest.TestCase):
    def test_reach_and_close_range(self):
        self.assertEqual(choose("sword", "bow", "sword", 60, 1.0, {})[0], "bow")
        self.assertEqual(choose("sword", "bow", "bow", 20, 1.0, {"sword": 2})[0], "sword")
        weapon, why = choose("sword", "bow", "sword", 60, 1.0, {})
        self.assertIn("60 px", why)

    def test_far_enemy_takes_the_long_weapon(self):
        # Beyond the bow's own reach too: ready it rather than walk in with the sword.
        self.assertEqual(choose("sword", "bow", "sword", 120, 1.0, {"sword": 5})[0], "bow")
        self.assertEqual(choose("boomerang", "whip", "boomerang", 150, 1.0, {})[0], "whip")
        # Two melee weapons: the longer one (spear) for a far enemy.
        self.assertEqual(choose("glove", "spear", "glove", 100, 1.0, {"glove": 4})[0], "spear")
        # Beyond NEAR_PX nothing changes.
        self.assertEqual(choose("sword", "bow", "sword", 200, 1.0, {})[0], "sword")

    def test_whip_blind_spot_and_low_hp(self):
        self.assertEqual(choose("whip", "glove", "whip", 12, 1.0, {})[0], "glove")
        self.assertEqual(choose("sword", "bow", "sword", 20, 0.3, {})[0], "bow")

    def test_idle_training_and_ready(self):
        self.assertEqual(choose("sword", "axe", "sword", None, 1.0, {"sword": 3, "axe": 0})[0], "axe")
        self.assertEqual(choose("sword", "axe", "axe", None, 1.0, {"sword": 1, "axe": 1})[0], "sword")

    def test_hysteresis_at_the_melee_edge(self):
        # Sword reach 28: holding the sword, 32 px stays sword (band 8),
        # 40 px takes the bow; holding the bow, 24 px keeps it, 18 px is sword.
        self.assertEqual(choose("sword", "bow", "sword", 32, 1.0, {})[0], "sword")
        self.assertEqual(choose("sword", "bow", "sword", 40, 1.0, {})[0], "bow")
        self.assertEqual(choose("sword", "bow", "bow", 24, 1.0, {})[0], "bow")
        self.assertEqual(choose("sword", "bow", "bow", 18, 1.0, {})[0], "sword")


class TacticsTest(unittest.TestCase):
    def test_fight_loop_short_hit_knockback_long_then_back(self):
        now = [0.0]
        t = WeaponTactics(lambda: now[0])
        self.assertIsNone(t.decide("sword", "bow", "sword", 20, 1.0, {}))   # in its face
        now[0] = 0.3
        swap = t.decide("sword", "bow", "sword", 60, 1.0, {})                # hit back 40 px
        self.assertEqual(swap[0], "bow")
        self.assertIn("knocked back 40 px", swap[1])
        now[0] = 0.6
        self.assertIsNone(t.decide("sword", "bow", "bow", 15, 1.0, {}))     # combat cooldown
        now[0] = 1.2
        self.assertEqual(t.decide("sword", "bow", "bow", 15, 1.0, {})[0], "sword")
        now[0] = 1.3
        swap = t.decide("sword", "bow", "sword", 64, 1.0, {})                # knocked again
        self.assertEqual(swap[0], "bow")                                     # skips the wait

    def test_refused_switch_retries_at_once(self):
        now = [0.0]
        t = WeaponTactics(lambda: now[0])
        self.assertEqual(t.decide("sword", "bow", "sword", 90, 1.0, {})[0], "bow")
        t.refused()
        now[0] = 0.1
        self.assertEqual(t.decide("sword", "bow", "sword", 90, 1.0, {})[0], "bow")

    def test_out_of_a_fight_the_slow_cooldown_applies(self):
        now = [0.0]
        t = WeaponTactics(lambda: now[0])
        self.assertEqual(t.decide("sword", "axe", "axe", None, 1.0, {"sword": 1, "axe": 1})[0],
                         "sword")
        now[0] = 2.0
        self.assertIsNone(t.decide("sword", "axe", "sword", None, 1.0, {"sword": 3, "axe": 0}))
        now[0] = 4.5
        self.assertEqual(t.decide("sword", "axe", "sword", None, 1.0, {"sword": 3, "axe": 0})[0],
                         "axe")

    def test_cooldown_and_busy(self):
        now = [0.0]
        t = WeaponTactics(lambda: now[0])
        self.assertIsNone(t.decide("sword", "bow", "sword", 60, 1.0, {}, busy=True))
        self.assertEqual(t.decide("sword", "bow", "sword", 60, 1.0, {})[0], "bow")
        now[0] = 0.5
        self.assertIsNone(t.decide("sword", "bow", "bow", 15, 1.0, {}))   # cooldown
        now[0] = 1.0
        self.assertEqual(t.decide("sword", "bow", "bow", 15, 1.0, {})[0], "sword")


if __name__ == "__main__":
    unittest.main()

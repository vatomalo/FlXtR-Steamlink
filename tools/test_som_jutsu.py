"""Kawarimi / Shunshin timing for the AI."""
import unittest

from som_jutsu import JutsuTactics, SHUNSHIN_COOLDOWN_S


class Clock:
    def __init__(self):
        self.t = 100.0

    def __call__(self):
        return self.t


class JutsuTest(unittest.TestCase):
    def test_shunshin_for_ninja_with_sword_then_cooldown(self):
        clock = Clock()
        j = JutsuTactics(clock)
        self.assertEqual(j.decide(2, "sword", 60, 1.0, False)[0], "shunshin")
        self.assertIsNone(j.decide(2, "sword", 60, 1.0, False))
        clock.t += SHUNSHIN_COOLDOWN_S
        self.assertEqual(j.decide(2, "sword", 60, 1.0, False)[0], "shunshin")
        self.assertIsNone(JutsuTactics(clock).decide(2, "boomerang", 60, 1.0, False))
        self.assertEqual(JutsuTactics(clock).decide(2, "spear", 60, 1.0, False)[0], "shunshin")
        self.assertIsNone(JutsuTactics(clock).decide(0, "sword", 60, 1.0, False))

    def test_kawarimi_before_a_hit_or_at_low_hp(self):
        clock = Clock()
        j = JutsuTactics(clock)
        self.assertIsNone(j.decide(0, "sword", 30, 1.0, False))     # no threat yet
        self.assertIsNone(j.decide(0, "sword", 60, 1.0, True))      # too far
        self.assertEqual(j.decide(0, "sword", 30, 1.0, True)[0], "kawarimi")
        self.assertIsNone(j.decide(0, "sword", 30, 1.0, True))      # still armed
        clock.t += 4.0
        self.assertEqual(j.decide(1, "glove", 20, 0.3, False)[0], "kawarimi")
        self.assertIsNone(JutsuTactics(clock).decide(3, "axe", 20, 0.3, True))  # no dodge

    def test_evade_gambit_zero_never_casts_and_refusal_retries(self):
        clock = Clock()
        self.assertIsNone(JutsuTactics(clock).decide(0, "sword", 20, 0.2, True, evade_weight=0))
        j = JutsuTactics(clock)
        self.assertEqual(j.decide(0, "sword", 20, 0.2, True)[0], "kawarimi")
        j.refused("kawarimi")
        self.assertEqual(j.decide(0, "sword", 20, 0.2, True)[0], "kawarimi")


if __name__ == "__main__":
    unittest.main()

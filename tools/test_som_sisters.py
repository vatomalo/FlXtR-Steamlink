import unittest

from som_class_director import ClassDirector, SISTER_HOLD_S


class SisterTest(unittest.TestCase):
    def director(self, current=2, pinned=True):
        now = [100.0]
        d = ClassDirector(clock=lambda: now[0])
        d.current, d.pinned = current, pinned
        return d, now

    def switch(self, d, pick):
        d.switched(pick[0])
        return pick[0]

    def test_each_situation_has_its_sister(self):
        d, now = self.director()
        self.assertIsNone(d.choose("balanced", {}, 0, enemies=1, nearest=120))  # far: Ninjutsu
        # Ninjutsu keeps its own Taijutsu, never switches to Monk merely
        # because one opponent is close (November 19 design change).
        self.assertIsNone(d.choose("balanced", {}, 0, enemies=1, nearest=30))
        pick = d.choose("balanced", {}, 0, enemies=3, nearest=30)
        self.assertEqual(pick[0], 10)                                          # crowd: Bushido
        self.switch(d, pick)
        self.assertIsNone(d.choose("balanced", {}, 0, hp_fraction=0.3,
                                   enemies=1, nearest=30))                    # held 8 s
        now[0] += SISTER_HOLD_S + 0.1
        self.assertEqual(d.choose("balanced", {}, 0, hp_fraction=0.3, enemies=1,
                                  nearest=30)[0], 9)                          # hurt: Roguecraft

    def test_back_home_when_the_fight_is_over(self):
        d, now = self.director()
        self.switch(d, d.choose("balanced", {}, 0, enemies=3, nearest=30))
        now[0] += 1
        pick = d.choose("balanced", {}, 0, enemies=0)
        self.assertEqual(pick[0], 2)
        self.assertIn("back to Ninjutsu", pick[1])

    def test_other_classes_keep_the_old_rules(self):
        d, _ = self.director(current=3)                 # Magic, picked by the player
        self.assertIsNone(d.choose("balanced", {}, 0, enemies=1, nearest=30))

    def test_class_switching_off_switches_no_sister(self):
        d, _ = self.director()
        d.cooldown = None                               # combat gambit "Switch classes" 0
        self.assertIsNone(d.choose("balanced", {}, 0, enemies=1, nearest=30))


if __name__ == "__main__":
    unittest.main()

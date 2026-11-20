import pathlib
import re
import unittest

import som_weapon_styles as ws

HEADER = pathlib.Path(__file__).resolve().parent.parent / "src" / "som_weapon_styles.h"


class TestWeaponStyles(unittest.TestCase):
    def test_table_matches_the_c_header(self):
        text = HEADER.read_text()
        rows = re.findall(r'\{"([^"]+)",(\d+),(-?\d+),(\d+),(\d+),(\d+)\}', text)
        c = [ws.WeaponStyle(n, int(w), int(r), int(s), int(p), int(k))
             for n, w, r, s, p, k in rows]
        self.assertEqual(tuple(c), ws.STYLES)
        self.assertIn(f"#define SOM_STYLE_MAX_LEVEL {ws.MAX_LEVEL}u", text)
        self.assertIn(f"#define SOM_STYLES_PER_CLASS {ws.STYLES_PER_CLASS}u", text)

    def test_every_class_has_three_styles_on_one_weapon(self):
        self.assertEqual(len(ws.STYLES), len(ws.CLASSES) * ws.STYLES_PER_CLASS)
        for klass in range(len(ws.CLASSES)):
            fast, balanced, heavy = ws.class_styles(klass)
            if heavy.name != "Kusarigama":         # the ninja's flail style
                self.assertEqual(heavy.weapon_id, balanced.weapon_id)
            self.assertEqual(fast.weapon_id, balanced.weapon_id)
            self.assertEqual((balanced.speed_pct, balanced.power_pct), (100, 100))
            self.assertGreater(fast.speed_pct, 100)
            self.assertGreater(heavy.power_pct, 100)

    def test_bushido_and_attack_scaling(self):
        self.assertEqual([s.name for s in ws.class_styles(10)],
                         ["Tanto", "Katana", "Dai Katana"])
        self.assertEqual(ws.style(10).weapon, "sword")
        self.assertIsNone(ws.style(len(ws.CLASSES)))
        katana = ws.style(10)
        self.assertEqual(ws.attack(katana, 1, 3, 99), 3)
        self.assertEqual(ws.attack(katana, ws.MAX_LEVEL, 3, 99), 99)
        self.assertEqual(ws.attack(ws.style(10, ws.FAST), 1, 1, 1), 1)


if __name__ == "__main__":
    unittest.main()

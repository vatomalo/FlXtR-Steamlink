"""Party assists wait for verified native control changes."""
import unittest

from som_party_assist import PartyAssist, lead_slot


def state(lead=0, girl_hp=80, boy_hp=80, sprite_mp=20, enemy_hp=0):
    return {
        "pad_bindings": [1 << lead, 0, 0],
        "party": [
            {"availability_byte": 1, "hp": boy_hp, "max_hp": 100, "mp": 0},
            {"availability_byte": 1, "controller_binding": 0, "hp": girl_hp,
             "max_hp": 100, "mp": 15},
            {"availability_byte": 1, "controller_binding": 0, "hp": 80,
             "max_hp": 100, "mp": sprite_mp},
        ],
        "enemy_slots": ([{"active": True, "max_hp": enemy_hp}]
                        if enemy_hp else []),
    }


class PartyAssistTest(unittest.TestCase):
    def test_healer_switch_cast_and_return_are_verified(self):
        assist = PartyAssist()
        classes = {1: 4, 2: 3}
        self.assertEqual(assist.step(state(boy_hp=20), classes, 0), ("action", "select"))
        self.assertEqual(assist.step(state(boy_hp=20), classes, .1), ("pending", None))
        self.assertEqual(assist.step(state(lead=1, boy_hp=20), classes, .5),
                         ("control", "cast_heal"))
        self.assertEqual(assist.step(state(lead=1, boy_hp=20), classes, 1),
                         ("pending", None))
        self.assertEqual(assist.step(state(lead=1, boy_hp=20), classes, 2),
                         ("action", "select"))
        self.assertEqual(assist.step(state(lead=0, boy_hp=80), classes, 2.5),
                         ("pending", None))

    def test_sprite_attack_spell_is_a_wanted_assist(self):
        assist = PartyAssist()
        obs = state(enemy_hp=200)
        self.assertEqual(lead_slot(obs), 0)
        self.assertEqual(assist.step(obs, {1: 0, 2: 3}, 0),
                         ("action", "select"))
        self.assertEqual(assist.target, 2)
        self.assertEqual(assist.step(state(lead=1, enemy_hp=200), {1: 0, 2: 3}, .5),
                         ("action", "select"))
        self.assertEqual(assist.step(state(lead=2, enemy_hp=200), {1: 0, 2: 3}, 1),
                         ("control", "cast_attack"))

    def test_human_controlled_companion_is_never_taken(self):
        assist = PartyAssist()
        obs = state(boy_hp=20)
        obs["party"][1]["controller_binding"] = 2
        self.assertIsNone(assist.step(obs, {1: 4, 2: 0}, 0))
        obs["pad_bindings"][1] = 2
        self.assertIsNone(assist.step(obs, {1: 4, 2: 3}, 1))


if __name__ == "__main__":
    unittest.main()

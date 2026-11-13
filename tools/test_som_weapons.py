"""Weapon level and status decoding with Turbo cross-checks (synthetic WRAM)."""
import unittest

from som_memory import WRAM
from som_planner import MemoryPlanner
from som_qwen_bridge import status_note, weapon_note


def hero_wram(weapon_type=1, row=9, levels=b"\x01\x00\x00\x00", anim=None, stage=0,
              owned=0x40, status=0, hp=76, active=1):
    """Live layout read on Turbo 2026-11-13: sword tier 0, level 1."""
    data = bytearray(0x20000)
    base = 0xE000
    data[base] = active
    data[base + 0x182:base + 0x184] = hp.to_bytes(2, "little")
    data[base + 0x190:base + 0x192] = status.to_bytes(2, "little")
    data[base + 0x1C0:base + 0x1C4] = levels
    data[base + 0x19B] = stage
    data[base + 0x1E4] = weapon_type
    data[base + 0x1E8] = row
    data[base + 0x068] = weapon_type if anim is None else anim
    data[0xCC54:0xCC5C] = b"\xff" * 8
    if owned is not None:
        data[0xCC54 + weapon_type] = owned
    return WRAM(bytes(data))


class TestWeaponLevels(unittest.TestCase):
    def test_live_turbo_layout_decodes_and_verifies(self):
        weapon = hero_wram().weapons(0)
        self.assertEqual((weapon["type"], weapon["tier"], weapon["level"], weapon["verified"]),
                         ("sword", 0, 1, True))
        self.assertEqual(weapon["levels"]["glove"], 0)

    def test_even_types_use_the_high_nibble(self):
        weapon = hero_wram(weapon_type=2, row=20, levels=b"\x00\x35\x00\x00",
                           owned=0x42).weapons(0)
        self.assertEqual((weapon["type"], weapon["tier"], weapon["level"]), ("axe", 2, 3))
        self.assertEqual(weapon["levels"]["spear"], 5)
        self.assertTrue(weapon["verified"])

    def test_disagreeing_fields_fail_closed(self):
        cases = {"row_matches_type": dict(row=18),
                 "animation_type": dict(anim=3),
                 "levels_range": dict(levels=b"\x0c\x00\x00\x00"),
                 "charge_within_level": dict(stage=2),
                 "owned_and_equipped": dict(owned=0x00)}
        for check, change in cases.items():
            weapon = hero_wram(**change).weapons(0)
            self.assertFalse(weapon["verified"], check)
            self.assertIn(check, weapon["failed_checks"])
        self.assertIn("not verified", weapon_note(weapon))


class TestStatus(unittest.TestCase):
    def test_documented_effects_and_control_lock(self):
        status = hero_wram(status=0x2020).status(0)
        self.assertEqual(status["effects"], ["frozen", "poison"])
        self.assertTrue(status["controls_locked"])
        self.assertEqual(status_note(status), "frozen, poison, controls locked")
        self.assertFalse(hero_wram(status=0x2000).status(0)["controls_locked"])
        self.assertEqual(hero_wram(status=0x0800).status(0)["unknown_bits"], 0x0800)

    def test_dead_bit_with_hp_left_is_not_trusted(self):
        self.assertFalse(hero_wram(status=0x8000, hp=40).status(0)["verified"])
        self.assertTrue(hero_wram(status=0x8000, hp=0).status(0)["verified"])
        # Uninitialised companion slots carry 0x8000 with stale HP.
        self.assertTrue(hero_wram(status=0x8000, hp=40, active=0).status(0)["verified"])

    def test_planner_waits_while_controls_are_locked(self):
        state = {"weapons_enabled": True, "enemy_slots": [],
                 "party": [{"hp": 80, "max_hp": 100, "weapon_recharge": 0,
                            "status": {"verified": True, "controls_locked": True,
                                       "effects": ["frozen"]}}],
                 "tile_context": {"runtime_verified": True, "game_mode": "field",
                                  "radius": 1, "map_id": 131, "map_size_tiles": [8, 8],
                                  "actor_tile": [2, 3],
                                  "actor": {"x_px": 40, "y_px": 56, "layer": 1},
                                  "passability_rows": ["...", ".@.", "..."]}}
        planner = MemoryPlanner()
        for _ in range(10):
            decision = planner.decide(state)
        self.assertEqual((decision.action, decision.reason),
                         ("wait", "controls locked by status frozen"))
        self.assertEqual(planner.blocked_moves, {})
        state["party"][0]["status"]["verified"] = False
        self.assertNotEqual(planner.decide(state).action, "wait")


if __name__ == "__main__":
    unittest.main()

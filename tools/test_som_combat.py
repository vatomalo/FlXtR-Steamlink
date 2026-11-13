"""Charged attack macro and planner charge choice (no emulator needed)."""
import unittest

from som_combat import ChargeFailed, ChargeMacro
from som_planner import MemoryPlanner


def snapshot(stage=0, counter=0, recharge=0, hp=80, enemy_hp=100, level=1, mode="field"):
    data = bytearray(0x20000)
    data[0xE000] = 1                                      # hero active
    data[0x00E4], data[0x00E6] = (1, 0x0F) if mode != "transition" else (0, 0)
    data[0x00D0] = 1 if mode == "event" else 0
    data[0xE01A], data[0xE19B], data[0xE1ED] = counter, stage, recharge
    data[0xE182:0xE184] = hp.to_bytes(2, "little")
    data[0xE184:0xE186] = (100).to_bytes(2, "little")
    data[0xE1E4], data[0xE1E8], data[0xE068] = 1, 9, 1    # sword, tier 0
    data[0xE1C0] = level                                  # sword: low nibble
    data[0xCC54:0xCC5C] = b"\xff" * 8
    data[0xCC55] = 0x40
    enemy = 0xE800                                        # slot 2
    data[enemy] = 1
    data[enemy + 0x182:enemy + 0x184] = enemy_hp.to_bytes(2, "little")
    data[enemy + 0x184:enemy + 0x186] = (120).to_bytes(2, "little")
    return bytes(data)


class FakeLink:
    def __init__(self, frames):
        self.frames, self.sent, self.index = list(frames), [], 0

    def fetch_wram(self, require_marker=False):
        frame = self.frames[min(self.index, len(self.frames) - 1)]
        self.index += 1
        return frame

    def send_action(self, action):
        self.sent.append(action)


class FakeClock:
    def __init__(self):
        self.now = 0.0

    def __call__(self):
        return self.now

    def sleep(self, seconds):
        self.now += seconds


def macro(frames, stage=1, slot=2):
    clock = FakeClock()
    link = FakeLink(frames)
    return link, ChargeMacro(link, stage, slot, log=lambda _: None,
                             sleep=clock.sleep, clock=clock)


class TestChargeMacro(unittest.TestCase):
    def test_holds_until_the_stage_then_releases_and_measures_damage(self):
        frames = ([snapshot()] + [snapshot(counter=c) for c in (10, 30, 44)] +
                  [snapshot(stage=1)] + [snapshot(stage=1, recharge=80, enemy_hp=40)] * 30)
        link, charge = macro(frames)
        result = charge.run()
        self.assertEqual(link.sent, ["charge", "wait"])
        self.assertEqual(result, {"stage": 1, "interrupted": False, "damage": 60})

    def test_a_hit_while_charging_releases_at_once(self):
        frames = [snapshot(), snapshot(counter=20), snapshot(counter=24, hp=60)]
        link, charge = macro(frames)
        self.assertTrue(charge.run()["interrupted"])
        self.assertEqual(link.sent, ["charge", "wait"])

    def test_a_charge_that_never_starts_fails_and_releases(self):
        link, charge = macro([snapshot()] * 100)
        with self.assertRaisesRegex(ChargeFailed, "never started"):
            charge.run()
        self.assertEqual(link.sent[-1], "wait")

    def test_stage_above_the_weapon_level_is_refused_before_pressing(self):
        link, charge = macro([snapshot(level=1)], stage=2)
        with self.assertRaises(ChargeFailed):
            charge.run()
        self.assertEqual(link.sent, [])

    def test_no_attack_after_release_is_a_failure(self):
        frames = [snapshot(), snapshot(counter=40), snapshot(stage=1)] + [snapshot(stage=1)] * 40
        link, charge = macro(frames)
        with self.assertRaisesRegex(ChargeFailed, "no attack"):
            charge.run()


def fight(hero_hp=80, enemy_hp=100, level=1, verified=True):
    return {"weapons_enabled": True,
            "party": [{"weapon_recharge": 0, "hp": hero_hp, "max_hp": 100,
                       "weapon": {"verified": verified, "level": level, "type": "sword"}}],
            "enemy_slots": [{"slot": 2, "active": True, "identity": 5, "x_px": 56,
                             "y_px": 40, "hp": enemy_hp, "hp_plausible": True}],
            "tile_context": {"runtime_verified": True, "game_mode": "field", "radius": 1,
                             "map_id": 129, "map_size_tiles": [8, 8], "actor_tile": [2, 2],
                             "actor": {"x_px": 40, "y_px": 40, "layer": 1,
                                       "facing": "step_right"},
                             "passability_rows": ["...", ".@.", "..."]}}


class TestChargeChoice(unittest.TestCase):
    def planner(self):
        planner = MemoryPlanner()
        planner.facing_checks = [5, 0]
        return planner

    def test_charges_a_sturdy_enemy_when_healthy_and_facing_it(self):
        decision = self.planner().decide(fight())
        self.assertEqual((decision.action, decision.charge_stage, decision.enemy_slot),
                         ("charge", 1, 2))

    def test_plain_swing_when_charging_is_not_worth_it_or_unsafe(self):
        for case in (dict(enemy_hp=20), dict(level=0),
                     dict(verified=False)):
            self.assertEqual(self.planner().decide(fight(**case)).action, "attack", case)
        planner = self.planner()
        planner.charge_disabled = True
        self.assertEqual(planner.decide(fight()).action, "attack")

    def test_stage_is_capped(self):
        self.assertEqual(self.planner().decide(fight(level=8)).charge_stage, 2)


if __name__ == "__main__":
    unittest.main()

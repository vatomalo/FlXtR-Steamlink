"""Closed-loop charged attack: hold the attack button until a verified stage.

som-decomp docs/weapons-melee.md 3: while the attack button (B) stays held
after a swing, the charge counter E01A counts one per two frames up to 44,
then the stage +0x19B rises by one (90 frames per stage), capped by the
weapon level; releasing the button performs the stage-N attack (damage
x(2N+4)/4). A hit cuts the charge. Nothing here writes game memory: the proxy
holds B ("charge") and any later command ("wait") releases it.
"""
import time

from som_memory import WRAM

HERO = 0x7EE000
CHARGE_COUNTER = HERO + 0x01A
CHARGE_STAGE = HERO + 0x19B
RECHARGE = HERO + 0x1ED
# The swing plus the weapon gauge come first (about 1.5 s), then 1.5 s per
# stage; generous limits so only a non-working charge is reported.
START_TIMEOUT = 3.0
STAGE_TIMEOUT = 2.5
RELEASE_TIMEOUT = 2.0
DAMAGE_WINDOW = 1.5


class ChargeFailed(Exception):
    pass


class ChargeMacro:
    POLL = 0.1

    def __init__(self, link, target_stage, enemy_slot=None, log=print,
                 sleep=time.sleep, clock=time.monotonic):
        self.link, self.target, self.enemy_slot = link, int(target_stage), enemy_slot
        self.log, self.sleep, self.clock = log, sleep, clock

    def _read(self):
        raw = self.link.fetch_wram(True)
        if raw is None:
            raise ChargeFailed("physical takeover or game exit")
        return WRAM(raw)

    def _enemy_hp(self, wram):
        if self.enemy_slot is None:
            return None
        return wram.enemy(self.enemy_slot - 1)["hp"]

    def _release(self):
        self.link.send_action("wait")

    def run(self):
        """Returns {"stage", "interrupted", "damage"}; raises ChargeFailed."""
        wram = self._read()
        weapon = wram.weapons(0)
        if not weapon["verified"] or not 1 <= self.target <= (weapon["level"] or 0):
            raise ChargeFailed(f"stage {self.target} not allowed by weapon {weapon}")
        if wram.u8(RECHARGE) != 0 or wram.game_mode() != "field":
            raise ChargeFailed("weapon not ready or not in field mode")
        hp_before = wram.character(0)["hp"]
        self.link.send_action("charge")
        started = self.clock()
        counted = False
        stage = 0
        try:
            while True:
                self.sleep(self.POLL)
                wram = self._read()
                if wram.game_mode() != "field":
                    raise ChargeFailed(f"left field mode ({wram.game_mode()}) while charging")
                stage = wram.u8(CHARGE_STAGE)
                counted = counted or wram.u8(CHARGE_COUNTER) > 0 or stage > 0
                if wram.character(0)["hp"] < hp_before:
                    self._release()
                    return {"stage": stage, "interrupted": True, "damage": None}
                if stage >= self.target:
                    break
                elapsed = self.clock() - started
                if not counted and elapsed > START_TIMEOUT:
                    raise ChargeFailed("charge counter never started")
                if elapsed > START_TIMEOUT + STAGE_TIMEOUT * self.target:
                    raise ChargeFailed(f"stage {stage} after {elapsed:.1f}s, wanted "
                                       f"{self.target}")
        except ChargeFailed:
            self._release()
            raise
        enemy_before = self._enemy_hp(wram)
        self._release()
        deadline = self.clock() + RELEASE_TIMEOUT
        while True:
            self.sleep(self.POLL)
            wram = self._read()
            if wram.u8(RECHARGE) > 0:
                break
            if self.clock() >= deadline:
                raise ChargeFailed(f"no attack after releasing stage {stage}")
        damage = None
        if enemy_before is not None:
            end = self.clock() + DAMAGE_WINDOW
            while self.clock() < end:
                self.sleep(self.POLL)
                wram = self._read()
            after = self._enemy_hp(wram)
            if isinstance(after, int) and after <= enemy_before:
                damage = enemy_before - after
        return {"stage": stage, "interrupted": False, "damage": damage}

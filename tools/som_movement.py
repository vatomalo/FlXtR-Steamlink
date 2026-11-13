"""Verify each short walking pulse from WRAM before changing direction.

Water, edge tiles, and diagonal half-tiles may need a second discrete
directional input. We infer this from a missing position acknowledgement,
not from an unverified hard-coded water tile ID.
"""
import time

STEPS = frozenset(("step_up", "step_down", "step_left", "step_right"))
WAIT_FOR_MOVEMENT = 0.42
MAX_RETRIES = 1


class MovementAck:
    def __init__(self, clock=None):
        self.clock = clock or time.monotonic
        self.pending = None

    def decide(self, desired, observation, intent="", allowed_steps=None):
        """Return (action_or_None, reason). Never bypass caller's tile guard.

        Only steps that were already authorized by the guard can be retried.
        A new intent involving survival, cutting, or a non-field mode cancels
        an outstanding walk. After one retry, replan normally.
        """
        context = (observation or {}).get("tile_context") or {}
        actor = context.get("actor") or {}
        valid = (context.get("runtime_verified") and
                 context.get("game_mode") == "field" and
                 all(isinstance(actor.get(k), int) for k in ("x_px", "y_px")))
        if not valid or intent in ("survive", "evade_enemy", "cut_grass", "charge_attack", "talk_to_npc"):
            self.pending = None
            return desired, "override"
        pos = (context.get("map_id"), actor["x_px"], actor["y_px"])
        if self.pending is not None:
            pending = self.pending
            if allowed_steps is not None and pending["action"] not in allowed_steps:
                self.pending = None
                return desired, "old direction no longer passable"
            if pos != pending["position"]:
                if (pos[0] == pending["position"][0] and desired in STEPS and
                        desired != pending["action"] and pending.get("centering", 0) < 2 and
                        self.clock()-pending["time"] < 1.2):
                    axis = 1 if pending["action"] in ("step_left", "step_right") else 2
                    positive = pending["action"] in ("step_right", "step_down")
                    fraction = pos[axis] % 16
                    before_center = fraction < 8 if positive else fraction > 8
                    if before_center:
                        pending["centering"] = pending.get("centering", 0)+1
                        return pending["action"], "finish tile before turning"
                self.pending = None
            elif desired not in STEPS:
                self.pending = None
                return desired, "override"
            elif self.clock() - pending["time"] < WAIT_FOR_MOVEMENT:
                return None, "awaiting verified displacement"
            elif pending["retries"] < MAX_RETRIES:
                pending["retries"] += 1
                pending["time"] = self.clock()
                return pending["action"], "retry same direction: no WRAM displacement"
            else:
                self.pending = None
                # One retry was sent. Do not loop forever on a true wall.
                return None, "retry failed: release and allow replanning"
        if desired in STEPS:
            self.pending = {"position": pos, "action": desired,
                            "time": self.clock(), "retries": 0}
        return desired, "new action"

    def cancel(self):
        self.pending = None

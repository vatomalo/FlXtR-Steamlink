"""Regression tests for two discrete movement pulses on slow terrain."""
import unittest
from som_movement import MovementAck


def observation(x=100, mode="field", verified=True):
    return {"tile_context": {"runtime_verified": verified, "game_mode": mode,
                             "map_id": 129, "actor": {"x_px": x, "y_px": 80}}}


class TestMovementAck(unittest.TestCase):
    def setUp(self):
        self.now = 0.0
        self.ack = MovementAck(clock=lambda: self.now)

    def test_second_press_after_no_displacement(self):
        self.assertEqual(self.ack.decide("step_up", observation())[0], "step_up")
        self.now = 0.25
        self.assertIsNone(self.ack.decide("step_right", observation())[0])
        self.now = 0.50
        action, reason = self.ack.decide("step_right", observation())
        self.assertEqual(action, "step_up")
        self.assertIn("retry", reason)
        self.now = 1.0
        self.assertIsNone(self.ack.decide("step_right", observation())[0])
        self.now = 1.5
        self.assertEqual(self.ack.decide("step_right", observation())[0], "step_right")

    def test_displacement_acknowledges_first_press(self):
        self.ack.decide("step_up", observation())
        self.now = 0.1
        self.assertEqual(self.ack.decide("step_right", observation(x=101))[0],
                         "step_right")

    def test_never_retries_outside_verified_field(self):
        self.ack.decide("step_up", observation())
        self.now = 2
        self.assertEqual(self.ack.decide("wait", observation(mode="event"))[0], "wait")
        self.assertIsNone(self.ack.pending)

    def test_survival_and_cutting_override_pending_walk(self):
        for intention in ("survive", "cut_grass"):
            self.ack.decide("step_up", observation())
            self.now += 0.1
            self.assertEqual(
                self.ack.decide("step_left", observation(), intention)[0],
                "step_left")
            self.assertIsNone(self.ack.pending)


if __name__ == "__main__":
    unittest.main()

"""Exercise automatic post-death dialogue without emulator or controller."""
import unittest

from som_respawn import RespawnDialogue


def state(mode, hp=50, effects=(), verified=True):
    return {"tile_context": {"game_mode": mode},
            "party": [{"hp": hp, "status": {"verified": verified,
                                          "effects": list(effects)}}]}


class RespawnDialogueTests(unittest.TestCase):
    def setUp(self):
        self.time = 0.0
        self.agent = RespawnDialogue(clock=lambda: self.time, max_confirms=3)

    def tick(self, mode, hp=50, effects=(), event=5, advance=0.0, verified=True):
        self.time += advance
        return self.agent.observe(state(mode, hp, effects, verified), event)

    def test_no_input_on_normal_story_events(self):
        for i in range(20):
            self.assertIsNone(self.tick("event", advance=2.0))
        self.assertEqual(self.agent.confirms, 0)

    def test_wait_for_verified_dead_transition_and_recovered_hp(self):
        self.assertIsNone(self.tick("event", 0, ("dead",)))
        self.assertIsNone(self.tick("transition", 0, ("dead",), advance=2))
        self.assertIsNone(self.tick("transition", 0, ("dead",), advance=2))
        self.assertIsNone(self.tick("event", 0, ("dead",), advance=2))
        self.assertIsNone(self.tick("event", 50, advance=0.2))
        self.assertIsNone(self.tick("event", 50, advance=1))
        self.assertEqual(self.tick("event", 50, advance=0.6), "confirm")
        self.assertEqual(self.agent.confirms, 1)

    def test_confirm_edge_spacing_progress_and_limit(self):
        self.tick("event", 0, ("dead",))
        self.tick("transition", 0, ("dead",))
        self.tick("event", 50)
        self.assertEqual(self.tick("event", advance=2), "confirm")
        self.assertIsNone(self.tick("event", advance=0.1))
        self.assertEqual(self.tick("event", advance=1.2), "confirm")
        self.assertEqual(self.tick("event", advance=1.2), "confirm")
        self.assertIsNone(self.tick("event", advance=2))
        self.assertTrue(self.agent.finished)

    def test_restart_into_event_without_death_never_confirms(self):
        fresh = RespawnDialogue(clock=lambda: 100.0)
        for _ in range(15):
            self.assertIsNone(fresh.observe(state("event")))

    def test_field_recovery_clears_state(self):
        self.tick("event", 0, ("dead",))
        self.tick("transition", 0, ("dead",))
        self.tick("event", 50)
        self.tick("event", advance=2)
        self.assertIsNone(self.tick("field", advance=1))
        self.assertEqual(self.agent.stage, "idle")
        self.assertEqual(self.agent.confirms, 0)

    def test_changed_event_needs_new_stable_interval(self):
        self.tick("event", 0, ("dead",))
        self.tick("transition", 0, ("dead",))
        self.tick("event", 50)
        self.assertIsNone(self.tick("event", event=6, advance=2))
        self.assertIsNone(self.tick("event", event=6, advance=1.1))
        self.assertEqual(self.tick("event", event=6, advance=0.5), "confirm")


if __name__ == "__main__":
    unittest.main()

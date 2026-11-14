"""Personality bubble event/cooldown tests; no emulator required."""
import unittest
from som_personality import Personality, LINES


def observation(map_id=131, hp=100, mode="field", verified=True):
    return {"tile_context": {"runtime_verified": verified, "game_mode": mode,
                             "map_id": map_id},
            "party": [{"hp": hp, "max_hp": 100}]}


class TestPersonality(unittest.TestCase):
    def test_each_style_produces_short_ascii_lines(self):
        for name in LINES:
            voice = Personality(name, seed=1)
            for index, event in enumerate(LINES[name]):
                line = voice.speak(event, now=100 + index * 25)
                self.assertIsNotNone(line)
                self.assertLessEqual(len(line), 32)
                self.assertTrue(line.isascii())

    def test_cooldowns_and_no_duplicate_spam(self):
        voice = Personality("dry", seed=1)
        self.assertIsNotNone(voice.speak("stuck", now=100))
        self.assertIsNone(voice.speak("stuck", now=110))
        self.assertIsNone(voice.speak("stuck", now=113))
        self.assertIsNotNone(voice.speak("stuck", now=125))

    def test_observation_events_only_when_verified(self):
        voice = Personality()
        self.assertIsNone(voice.observation_event(observation()))
        self.assertEqual(voice.observation_event(observation(hp=70)), "hurt")
        self.assertIsNone(voice.observation_event(observation(mode="event")))
        self.assertIsNone(voice.observation_event(observation(hp=20, verified=False)))
        self.assertEqual(voice.observation_event(observation(map_id=130, hp=70)),
                         "map_change")

    def test_persistent_visit_changes_map_change_to_return(self):
        voice = Personality()
        self.assertIsNone(voice.observation_event(observation(map_id=131)))
        self.assertEqual(voice.observation_event(
            observation(map_id=130), visited_before=True), "map_return")
        self.assertEqual(voice.observation_event(
            observation(map_id=129), visited_before=False), "map_change")

    def test_every_spoken_moment_has_varied_lines_in_every_style(self):
        from som_qwen_bridge import SPOKEN_INTENTIONS
        for style, events in LINES.items():
            for event in SPOKEN_INTENTIONS | {"map_change", "map_return", "hurt", "victory"}:
                self.assertIn(event, events, (style, event))
            for event, lines in events.items():
                self.assertGreaterEqual(len(lines), 2, (style, event))
                self.assertEqual(len(set(lines)), len(lines), (style, event))
                for line in lines:
                    self.assertNotIn("'", line)      # stripped by the Steam Link filter

    def test_no_unrecognized_events(self):
        self.assertIsNone(Personality().speak("cast_fire"))


if __name__ == "__main__":
    unittest.main()

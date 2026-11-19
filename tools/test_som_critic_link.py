import json, os, tempfile, time, unittest
from pathlib import Path
from unittest import mock

from som_critic_link import critic_coach, critic_tags, nudge_style, read_critic


class TestCriticLink(unittest.TestCase):
    def write(self, directory, **data):
        path = Path(directory) / "critic.json"
        path.write_text(json.dumps(data))
        return path

    def test_off_unless_enabled(self):
        with tempfile.TemporaryDirectory() as d, mock.patch.dict(os.environ, {}, clear=True):
            self.assertIsNone(read_critic(self.write(d, time=time.time(), coach="x")))

    def test_stale_verdict_ignored(self):
        with tempfile.TemporaryDirectory() as d, \
                mock.patch.dict(os.environ, {"SOM_AI_CRITIC": "1"}):
            path = self.write(d, time=time.time() - 600, coach="x", tags=["no_evade"])
            self.assertIsNone(read_critic(path))

    def test_coach_is_plain_words(self):
        self.assertEqual(critic_coach({"coach": "Stop!\n<b>pacing</b> `rm -rf`"}),
                         "Stop b pacing /b rm -rf")

    def test_tags_nudge_weights_but_never_turn_on_a_zero(self):
        style = {"press": 3, "distance": 2, "guard": 0, "evade": 2, "charge": 5}
        new, changed = nudge_style(style, critic_tags({"tags": ["no_evade", "mashing", "bogus"]}))
        self.assertEqual(new["evade"], 3)        # now above guard: evade instead of block
        self.assertEqual(new["guard"], 0)        # player turned guarding off
        self.assertEqual(new["charge"], 5)       # capped
        self.assertEqual(changed, ["evade"])
        self.assertEqual(style["evade"], 2)      # input untouched


if __name__ == "__main__":
    unittest.main()

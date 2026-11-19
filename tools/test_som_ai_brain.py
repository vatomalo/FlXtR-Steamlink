"""Model picker and critic regression tests, entirely offline."""
import io
import json
from pathlib import Path
import re
import time
import unittest
from unittest.mock import patch

from som_ai_brain import AI_MODELS, BrainController, ensure_model, parse_brain
from som_critic import Critic


class TestBrainSettings(unittest.TestCase):
    def test_model_labels_match_game_menu_order(self):
        header = (Path(__file__).resolve().parents[1] / "src/som_gambit.h").read_text()
        block = re.search(r"som_ai_model_names\[SOM_AI_MODEL_COUNT\]\s*=\s*\{(.*?)\};",
                          header, flags=re.S)
        self.assertIsNotNone(block)
        labels = re.findall(r'"([^"]+)"', block.group(1))
        self.assertEqual(labels, [label for label, _ in AI_MODELS])
        self.assertEqual(len(set(tag for _, tag in AI_MODELS)), len(AI_MODELS))

    def test_brain_parser_fails_closed(self):
        self.assertEqual(parse_brain("0 1"), (0, True))
        self.assertEqual(parse_brain("14 0\n"), (14, False))
        for invalid in ("", "15 0", "0 2", "-1 0", "4 1 extra", "x y", None):
            self.assertEqual(parse_brain(invalid), (0, True))

    def test_installed_model_is_not_pulled(self):
        tags = {"models": [{"name": "qwen3:0.6b"}]}
        with patch("som_ai_brain.urllib.request.urlopen",
                   return_value=io.BytesIO(json.dumps(tags).encode())) as request:
            self.assertTrue(ensure_model("http://localhost:11434", "qwen3:0.6b"))
            self.assertEqual(request.call_count, 1)

    def test_missing_model_pulls_with_progress(self):
        tags = io.BytesIO(b'{"models": []}')
        result = io.BytesIO(
            b'{"status":"pulling","total":100,"completed":42}\n'
            b'{"status":"pulling","total":100,"completed":98}\n'
            b'{"status":"success"}\n')
        with patch("som_ai_brain.urllib.request.urlopen", side_effect=[tags, result]) as request:
            progress = []
            self.assertTrue(ensure_model("http://127.0.0.1:11434",
                                         "qwen3:1.7b", progress.append))
            self.assertEqual(request.call_count, 2)
            self.assertIn("42%", progress)
            self.assertIn("98%", progress)

    def test_download_rejects_non_allowlisted_model_and_remote_urls(self):
        with self.assertRaises(ValueError):
            ensure_model("http://localhost:11434", "arbitrary/untrusted:latest")
        with self.assertRaises(ValueError):
            ensure_model("https://example.org", "qwen3:0.6b")

    def test_worker_switches_only_when_download_is_complete(self):
        controller = BrainController("http://localhost:11434", "qwen3:0.6b")
        with patch("som_ai_brain.ensure_model", return_value=True):
            controller.select(1, False)
            for _ in range(100):
                notices = controller.drain()
                if controller.model == "qwen3:1.7b":
                    break
                time.sleep(0.01)
            else:
                self.fail("model worker did not return")
        self.assertFalse(controller.critic)
        self.assertTrue(any("AI ready" in notice for notice in notices))
        self.assertEqual(controller.model, "qwen3:1.7b")


class TestCritic(unittest.TestCase):
    def test_critic_disabled_by_default(self):
        critic = Critic("http://localhost:11434")
        self.assertFalse(critic.enabled)
        self.assertFalse(critic.offer({"party": [{"hp": 33}]}, "wait"))

    def test_telemetry_uses_measured_fields(self):
        facts = Critic.evidence(
            {"party": [{"hp": 12, "max_hp": 100}],
             "tile_context": {"map_id": 131, "game_mode": "field",
                              "runtime_verified": True},
             "enemy_slots": [{"active": True}, {"active": False}]}, "attack")
        self.assertEqual(facts["hp"], 12)
        self.assertEqual(facts["map"], 131)
        self.assertEqual(facts["active_enemies"], 1)
        self.assertEqual(facts["action"], "attack")


if __name__ == "__main__":
    unittest.main()

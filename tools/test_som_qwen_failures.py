"""Qwen as an optional advisor: every failure is a clean exception the
bridge turns into "keep the current goal" (no emulator, a local fake server)."""
import http.server
import json
import threading
import unittest
from unittest import mock

import som_qwen_bridge as bridge


class FakeOllama(http.server.BaseHTTPRequestHandler):
    reply = b"{}"
    delay = 0.0

    def do_POST(self):
        self.rfile.read(int(self.headers.get("Content-Length", 0)))
        if FakeOllama.delay:
            threading.Event().wait(FakeOllama.delay)
        self.send_response(200)
        self.end_headers()
        self.wfile.write(FakeOllama.reply)

    def log_message(self, *args):
        pass


class QwenFailureTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), FakeOllama)
        threading.Thread(target=cls.server.serve_forever, daemon=True).start()
        cls.url = "http://127.0.0.1:%d" % cls.server.server_address[1]

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()

    def ask(self):
        return bridge.choose_goal(self.url, "qwen", "play", {}, ["hunt_here", "go_to_inn"],
                                  "hunt_here")

    def test_good_answer(self):
        FakeOllama.reply, FakeOllama.delay = json.dumps({"response": "go_to_inn"}).encode(), 0
        self.assertEqual(self.ask(), "go_to_inn")

    def test_goal_outside_the_allowlist(self):
        FakeOllama.reply, FakeOllama.delay = json.dumps({"response": "delete_save"}).encode(), 0
        with self.assertRaises(ValueError):
            self.ask()

    def test_malformed_and_non_object_replies(self):
        for reply in (b"not json", b"[1, 2]", b'"text"', b"null"):
            FakeOllama.reply, FakeOllama.delay = reply, 0
            with self.subTest(reply=reply), self.assertRaises(ValueError):
                self.ask()

    def test_timeout_is_an_oserror(self):
        FakeOllama.reply, FakeOllama.delay = json.dumps({"response": "go_to_inn"}).encode(), 1.5
        with mock.patch.object(bridge, "DIRECTOR_TIMEOUT", 0.3), self.assertRaises(OSError):
            self.ask()

    def test_stale_answers_expire(self):
        self.assertGreater(bridge.DIRECTOR_MAX_AGE, 0)
        self.assertLess(bridge.DIRECTOR_MAX_AGE, 600)


if __name__ == "__main__":
    unittest.main()

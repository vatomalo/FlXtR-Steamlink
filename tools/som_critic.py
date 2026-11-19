"""Persistent, low-frequency read-only critic for Secret of Mana Ultimate.

The critic does not issue controller commands. It studies verified observations,
writes concise game coaching to a JSONL journal and optionally to the overlay.
Enabled/disabled by the in-game Critic setting, not an environment drop-in.
"""
import json
import os
from pathlib import Path
import queue
import re
import threading
import time
import urllib.request

from som_ai_brain import _request, ensure_model


class Critic:
    def __init__(self, url, model=None, interval=90, journal=None):
        self.url = url
        self.model = model or os.environ.get("SOM_AI_CRITIC_MODEL", "qwen3:0.6b")
        self.interval = interval
        self.journal = (Path(journal) if journal is not None else
                        Path.home() / ".local/state/flxtr/som-critic.jsonl")
        self.enabled = False
        self._pending = False
        self._last = float("-inf")
        self._events = queue.Queue()

    @staticmethod
    def evidence(observation, action):
        """Only measured gameplay telemetry enters the critic prompt."""
        obs = observation if isinstance(observation, dict) else {}
        party = obs.get("party") or [{}]
        hero = party[0] if isinstance(party[0], dict) else {}
        tiles = obs.get("tile_context") or {}
        enemies = obs.get("enemy_slots") or []
        return {
            "map": tiles.get("map_id"),
            "mode": tiles.get("game_mode"),
            "hp": hero.get("hp"),
            "max_hp": hero.get("max_hp"),
            "active_enemies": sum(bool(e.get("active")) for e in enemies
                                  if isinstance(e, dict)),
            "action": action,
            "verified_tiles": bool(tiles.get("runtime_verified")),
        }

    def offer(self, observation, action, now=None):
        if not self.enabled or self._pending or not isinstance(observation, dict):
            return False
        now = time.monotonic() if now is None else now
        if now - self._last < self.interval:
            return False
        self._pending, self._last = True, now
        snapshot = self.evidence(observation, action)
        threading.Thread(target=self._judge, args=(snapshot,), daemon=True,
                         name="som-critic").start()
        return True

    def _judge(self, facts):
        try:
            # A lightweight independent critic model is installed on demand.
            if not ensure_model(self.url, self.model):
                raise OSError("critic model download interrupted")
            prompt = (
                "You are the read-only coach for a Secret of Mana game AI. "
                "Use ONLY this verified telemetry; missing values are unknown. "
                "Give one short, useful gameplay criticism or praise, under "
                "eight words. Never invent locations, quests or achievements. "
                "Do not output controller commands. Telemetry: " +
                json.dumps(facts, separators=(",", ":")))
            request = _request(self.url, "/api/generate", {
                "model": self.model, "prompt": prompt, "stream": False,
                "think": False, "keep_alive": "30s",
                "options": {"temperature": 0.1, "num_predict": 40, "num_ctx": 512}})
            with urllib.request.urlopen(request, timeout=60) as response:
                raw = json.load(response).get("response", "")
            note = re.sub(r"<think>.*?(</think>|$)", "", str(raw), flags=re.S | re.I)
            note = re.sub(r"[^A-Za-z0-9 .,!?':-]", "", note).strip()[:50]
            if note:
                record = {"time": time.time(), "model": self.model,
                          "telemetry": facts, "critique": note}
                try:
                    self.journal.parent.mkdir(parents=True, exist_ok=True)
                    with self.journal.open("a", encoding="utf-8") as out:
                        out.write(json.dumps(record, separators=(",", ":")) + "\n")
                except OSError as exc:
                    print(f"[critic] journal unavailable: {exc}", flush=True)
                self._events.put(note)
        except (OSError, ValueError, TimeoutError) as exc:
            print(f"[critic] unavailable: {exc}", flush=True)
        finally:
            self._pending = False

    def drain(self):
        notes = []
        while True:
            try:
                note = self._events.get_nowait()
            except queue.Empty:
                break
            if self.enabled:
                notes.append(note)
        return notes

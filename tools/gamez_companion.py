#!/usr/bin/env python3
"""CafeLeBlanc Gamez companion. Windows Python 3, standard library only.

Run on the Windows host, not on the Steam Link. Deliberately does not edit
Sunshine's application database. The original FFVII entry is preserved until
the user explicitly configures a dedicated dummy session.
"""
import hmac
import json
import os
from pathlib import Path
import subprocess
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

ROOT = Path(__file__).resolve().parent
CONFIG = ROOT / "gamez.local.json"
lock = threading.Lock()
current = None

def configuration():
    data = json.loads(CONFIG.read_text(encoding="utf-8"))
    secret = data.get("token", "")
    if not isinstance(secret, str) or len(secret) < 32:
        raise ValueError("gamez.local.json requires a random token of at least 32 characters")
    return data

class Handler(BaseHTTPRequestHandler):
    def reply(self, code, message):
        payload = json.dumps({"status": message}).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def do_POST(self):
        global current
        if self.path != "/v1/launch":
            return self.reply(404, "unknown endpoint")
        try:
            data = configuration()
            expected = "Bearer " + data["token"]
            if not hmac.compare_digest(self.headers.get("Authorization", ""), expected):
                return self.reply(401, "unauthorized")
            if int(self.headers.get("Content-Length", "0")) not in range(1, 33):
                return self.reply(400, "invalid length")
            game_id = self.rfile.read(int(self.headers["Content-Length"])).decode("ascii").strip()
            if not game_id.isascii() or not game_id.isdecimal():
                return self.reply(400, "invalid game id")
            entry = data.get("games", {}).get(game_id)
            if not isinstance(entry, dict):
                return self.reply(404, "unconfigured game")
            exe = Path(entry["exe"])
            if not exe.is_absolute() or exe.suffix.lower() != ".exe" or not exe.is_file():
                return self.reply(422, "game executable missing")
            workdir = Path(entry.get("cwd") or str(exe.parent))
            if not workdir.is_dir():
                return self.reply(422, "working directory missing")
            args = entry.get("args", [])
            if not isinstance(args, list) or not all(isinstance(x, str) for x in args):
                return self.reply(422, "invalid arguments")
            with lock:
                if current is not None and current.poll() is None:
                    return self.reply(409, "another game still running")
                # Never interpret network input as a command or executable path.
                current = subprocess.Popen([str(exe), *args], cwd=str(workdir), shell=False)
            return self.reply(200, "launched")
        except (OSError, ValueError, KeyError, UnicodeError, json.JSONDecodeError) as exc:
            self.log_error("Gamez request failed: %s", exc)
            return self.reply(422, "launch failed")

if __name__ == "__main__":
    config = configuration()
    host = config.get("bind", "0.0.0.0")
    port = int(config.get("port", 47777))
    print(f"Gamez companion ready on {host}:{port}; configure Windows firewall for Steam Link LAN only", flush=True)
    ThreadingHTTPServer((host, port), Handler).serve_forever()

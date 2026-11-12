#!/usr/bin/env python3
"""Experimental Qwen -> SSH -> FlXtR Libretro action bridge (manual opt-in).

Runs on SkyLoft with a LOCAL Ollama instance. Qwen currently receives *only*
the last action and a user-supplied objective; it has no live game vision or
verified state. This is a connectivity test, not context-aware gameplay.
"""
import argparse
import json
import subprocess
import sys
import time
import urllib.request

ACTIONS = {
    "right", "left", "up", "down", "attack", "open_ring",
    "confirm", "cancel", "next_ring", "prev_ring", "snapshot",
}

def choose_action(url: str, model: str, objective: str, previous: str) -> str:
    prompt = (
        "You are a primitive test controller for Secret of Mana Turbo. "
        "You cannot see the screen, character location, inventory or enemy state. "
        "Choose ONE safe, short controller action from: "
        + ", ".join(sorted(ACTIONS))
        + ". Reply with ONLY the lowercase action name. "
        "Prefer movement or attack, avoid menus unless asked. "
        f"Objective: {objective}. Previous action: {previous}."
    )
    # Qwen3 reasoning models may spend a tiny num_predict budget entirely
    # on hidden thinking, returning response="" despite successful inference.
    # Disable reasoning for this one-token action task, with a little headroom.
    payload = json.dumps({"model": model, "prompt": prompt, "stream": False,
                          "think": False,
                          "options": {"temperature": 0.0, "num_predict": 64}}).encode()
    req = urllib.request.Request(
        url.rstrip("/") + "/api/generate", data=payload,
        headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=90) as response:
        result = json.load(response)
    raw = result.get("response") or ""
    candidate = raw.strip().lower().strip("` \\"'\\r\\n")
    if candidate not in ACTIONS:
        reason = result.get("done_reason", "unknown")
        raise ValueError(
            f"Qwen returned unsupported action {candidate!r} "
            f"(done_reason={reason!r}, thinking_present={bool(result.get('thinking'))}). "
            "Try --model qwen3.5:4b if qwen3:0.6b gives no response."
        )
    return candidate

def send_action(host: str, action: str) -> None:
    # stdin carries an allowlisted command. No LLM text is interpolated into shell.
    cmd = ["ssh", "-o", "BatchMode=yes", "-o", "ConnectTimeout=7",
           host, "cat > /tmp/flxtr-som-ai-action"]
    subprocess.run(cmd, input=action + "\n", text=True, check=True, timeout=15)

def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="root@100.124.74.13")
    parser.add_argument("--ollama-url", default="http://127.0.0.1:11434")
    parser.add_argument("--model", default="qwen3:0.6b")
    parser.add_argument("--objective", default="Explore slowly and attack occasionally")
    parser.add_argument("--steps", type=int, default=5)
    parser.add_argument("--interval", type=float, default=2.0)
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()
    if not (1 <= args.steps <= 30) or args.interval < 1.0:
        parser.error("steps must be 1..30 and interval at least 1 second")
    if args.ollama_url not in ("http://127.0.0.1:11434", "http://localhost:11434"):
        parser.error("only local Ollama is supported for this initial experiment")
    previous = "none"
    for i in range(args.steps):
        try:
            action = choose_action(args.ollama_url, args.model,
                                   args.objective, previous)
            print(f"[qwen] {i+1}/{args.steps}: {action}", flush=True)
            if not args.dry_run:
                send_action(args.host, action)
            previous = action
            if i+1 < args.steps:
                time.sleep(args.interval)
        except (OSError, ValueError, subprocess.SubprocessError) as exc:
            print(f"[qwen] stopped safely: {exc}", file=sys.stderr)
            return 1
    return 0

if __name__ == "__main__":
    sys.exit(main())

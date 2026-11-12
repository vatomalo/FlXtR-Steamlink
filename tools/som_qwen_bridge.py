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

MOVES = ("right", "down", "left", "up")
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
    candidate = raw.strip().lower().strip("`").strip()
    if candidate not in ACTIONS:
        reason = result.get("done_reason", "unknown")
        raise ValueError(
            f"Qwen returned unsupported action {candidate!r} "
            f"(done_reason={reason!r}, thinking_present={bool(result.get('thinking'))}). "
            "Try --model qwen3.5:4b if qwen3:0.6b gives no response."
        )
    return candidate

def send_action(host: str, action: str) -> None:
    # stdin carries only an allowlisted single action or locally constructed plan.
    tokens = action.split()
    valid_plan = (len(tokens) >= 2 and tokens[0] == "plan" and
                  len(tokens) <= 13 and
                  all(token in (*MOVES, "attack") for token in tokens[1:]))
    if action not in ACTIONS and not valid_plan:
        raise ValueError("Refusing unsupported controller command")
    cmd = ["ssh", "-o", "BatchMode=yes", "-o", "ConnectTimeout=7",
           host, "cat > /tmp/flxtr-som-ai-action"]
    subprocess.run(cmd, input=action + "\n", text=True, check=True, timeout=15)

def make_plan(first: str, count: int) -> list[str]:
    """Qwen chooses a heading; the C runtime performs a bounded plan."""
    start = MOVES.index(first) if first in MOVES else 0
    directions = [MOVES[(start + i) % len(MOVES)] for i in range(4)]
    return [("attack" if i % 3 == 2 else directions[(i // 3) % 4])
            for i in range(count)]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="root@100.124.74.13")
    parser.add_argument("--ollama-url", default="http://127.0.0.1:11434")
    parser.add_argument("--model", default="qwen3:0.6b")
    parser.add_argument("--objective", default="Explore slowly and attack occasionally")
    parser.add_argument("--steps", type=int, default=5)
    parser.add_argument("--interval", type=float, default=2.0)
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--individual", action="store_true",
                        help="Legacy slower per-action Ollama calls")
    args = parser.parse_args()
    if not (1 <= args.steps <= 30) or args.interval < 1.0:
        parser.error("steps must be 1..30 and interval at least 1 second")
    if args.ollama_url not in ("http://127.0.0.1:11434", "http://localhost:11434"):
        parser.error("only local Ollama is supported for this initial experiment")
    if not args.individual:
        try:
            heading = choose_action(args.ollama_url, args.model, args.objective, "none")
            actions = make_plan(heading, min(args.steps, 12))
            command = "plan " + " ".join(actions)
            print("[qwen] heading=" + heading + "; plan=" + " ".join(actions), flush=True)
            if not args.dry_run:
                send_action(args.host, command)
                print("[ssh] plan submitted; check game.log for PLAN START", flush=True)
            return 0
        except (OSError, ValueError, subprocess.SubprocessError) as exc:
            print(f"[qwen] stopped safely: {exc}", file=sys.stderr)
            return 1
    previous = "none"
    move_cursor = 0
    for i in range(args.steps):
        try:
            action = choose_action(args.ollama_url, args.model,
                                   args.objective, previous)
            # Without live state the tiny model may choose "attack" forever.
            # Apply a deterministic cadence and varied walking directions.
            # This is intentionally NOT intelligent obstacle avoidance.
            raw_action = action
            if i % 3 != 2:
                if action not in MOVES or action == previous:
                    action = MOVES[move_cursor % len(MOVES)]
                move_cursor += 1
            elif action not in ("attack", *MOVES):
                action = "attack"
            if action == previous and action == "attack":
                action = MOVES[move_cursor % len(MOVES)]
                move_cursor += 1
            print(f"[qwen] {i+1}/{args.steps}: {action}"
                  + (f" (model said {raw_action})" if raw_action != action else ""),
                  flush=True)
            if not args.dry_run:
                send_action(args.host, action)
                print(f"[ssh] submitted {action} to {args.host} (not gameplay-confirmed)", flush=True)
            previous = action
            if i+1 < args.steps:
                time.sleep(args.interval)
        except (OSError, ValueError, subprocess.SubprocessError) as exc:
            print(f"[qwen] stopped safely: {exc}", file=sys.stderr)
            return 1
    return 0

if __name__ == "__main__":
    sys.exit(main())

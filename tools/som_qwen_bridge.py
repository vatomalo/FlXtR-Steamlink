#!/usr/bin/env python3
"""Experimental Qwen -> SSH -> FlXtR Libretro action bridge (manual opt-in).

Runs on SkyLoft with a LOCAL Ollama instance. Qwen receives the last action,
the user-supplied objective, and optionally a read-only WRAM observation. The
observation still lacks verified navigation fields, so this is not yet
context-aware autonomous gameplay.
"""
import argparse
import json
import subprocess
import sys
import time
import urllib.request
from pathlib import Path

from som_memory import WRAM, ObservationHistory

ACTIONS = {
    "step_right", "step_left", "step_up", "step_down", "wait",
    "attack", "open_ring", "confirm", "cancel", "next_ring",
    "prev_ring", "snapshot",
}
STEP_OFFSETS = {
    "step_right": (1, 0), "step_left": (-1, 0),
    "step_up": (0, -1), "step_down": (0, 1),
}

def choose_action(url: str, model: str, objective: str, previous: str, observation=None) -> str:
    prompt = (
        "You are a primitive test controller for Secret of Mana Turbo. "
        "You cannot see the screen, character location, inventory or enemy state. "
        "Choose ONE safe, short controller action from: "
        + ", ".join(sorted(ACTIONS))
        + ". Reply with ONLY the lowercase action name. "
        "Use only short step movement. The tile grid uses @ for the actor, "
        ". for open, # for blocked, and ? for conditional terrain. "
        "Prefer a verified open step or attack; wait when movement is unsafe. "
        "Avoid menus unless asked. "
        f"Objective: {objective}. Previous action: {previous}. "
        + ("Verified/partially decoded WRAM observation (unknown values are null; "
           "never assume an enemy slot is active): "
           + json.dumps(observation, separators=(",", ":")) if observation else
           "No WRAM observation available. Do not pretend to see the game.")
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

def guard_tile_action(action: str, observation) -> str:
    """Deterministically veto movement that lacks a verified open next tile."""
    context = observation.get("tile_context") if isinstance(observation, dict) else None
    if isinstance(context, dict) and context.get("game_mode") != "field":
        return "wait" if action in STEP_OFFSETS or action == "attack" else action
    if action not in STEP_OFFSETS:
        return action
    if not isinstance(context, dict) or not context.get("runtime_verified"):
        return "wait"
    rows = context.get("passability_rows")
    radius = context.get("radius")
    dx, dy = STEP_OFFSETS[action]
    if not isinstance(rows, list) or not isinstance(radius, int):
        return "wait"
    row = radius + dy
    column = radius + dx
    if not 0 <= row < len(rows) or not isinstance(rows[row], str) or not 0 <= column < len(rows[row]):
        return "wait"
    return action if rows[row][column] == "." else "wait"

def send_action(host: str, action: str) -> None:
    # stdin carries an allowlisted command. No LLM text is interpolated into shell.
    cmd = ["ssh", "-o", "BatchMode=yes", "-o", "ConnectTimeout=7",
           host, "cat > /tmp/flxtr-som-ai-action"]
    subprocess.run(cmd, input=action + "\n", text=True, check=True, timeout=15)

def remote_ring_ai_enabled(host: str) -> bool:
    try:
        result = subprocess.run(
            ["ssh", "-o", "BatchMode=yes", "-o", "ConnectTimeout=7", host,
             "test -f /tmp/flxtr-som-ai-enabled"],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=15)
        return result.returncode == 0
    except (OSError, subprocess.SubprocessError):
        return False

def clear_remote_ring_ai(host: str) -> None:
    """End one ring-controlled session using only the fixed marker path."""
    try:
        subprocess.run(
            ["ssh", "-o", "BatchMode=yes", "-o", "ConnectTimeout=7", host,
             "rm -f /tmp/flxtr-som-ai-enabled"],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=15,
            check=False)
    except (OSError, subprocess.SubprocessError):
        pass

def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="root@100.124.74.13")
    parser.add_argument("--ollama-url", default="http://127.0.0.1:11434")
    parser.add_argument("--model", default="qwen3:0.6b")
    parser.add_argument("--objective", default="Explore slowly and attack occasionally")
    parser.add_argument("--steps", type=int, default=5)
    parser.add_argument("--interval", type=float, default=2.0)
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--wram-file", type=Path,
                        help="Path to raw 128-KiB WRAM dump; reread each step")
    parser.add_argument("--wram-remote", action="store_true",
                        help="Read /tmp/flxtr-som-wram.bin on target via SSH per step")
    parser.add_argument("--history-db", type=Path,
                        help="Optional persistent SQLite observation log")
    parser.add_argument("--ring-control", action="store_true",
                        help="Wait for the in-game Action Grid start gesture and stop on takeover")
    parser.add_argument("--start-timeout", type=float, default=300.0,
                        help="Seconds to wait for Action Grid start when --ring-control is used")
    args = parser.parse_args()
    if not (0 <= args.steps <= 30) or args.interval < 1.0:
        parser.error("steps must be 1..30, or 0 for continuous ring control")
    if args.steps == 0 and not args.ring_control:
        parser.error("steps=0 is allowed only with --ring-control")
    if args.ollama_url not in ("http://127.0.0.1:11434", "http://localhost:11434"):
        parser.error("only local Ollama is supported for this initial experiment")
    if args.wram_file and args.wram_remote:
        parser.error("choose one of --wram-file or --wram-remote")
    if args.ring_control and args.dry_run:
        parser.error("--ring-control requires a live SSH target")
    if args.ring_control and not args.wram_remote:
        parser.error("--ring-control requires --wram-remote for fail-closed perception")
    if not 1 <= args.start_timeout <= 3600:
        parser.error("start-timeout must be 1..3600 seconds")
    history = ObservationHistory(args.history_db) if args.history_db else None
    if args.ring_control:
        print("[qwen] waiting for unchanged Action Grid confirmation", flush=True)
        deadline = time.monotonic() + args.start_timeout
        while not remote_ring_ai_enabled(args.host):
            if time.monotonic() >= deadline:
                print("[qwen] stopped safely: Action Grid start timed out", file=sys.stderr)
                return 1
            time.sleep(1.0)
        print("[qwen] Action Grid start received", flush=True)
    previous = "none"
    repeated = 0
    step = 0
    while args.steps == 0 or step < args.steps:
        try:
            if args.ring_control and not remote_ring_ai_enabled(args.host):
                print("[qwen] Action Grid AI stopped by physical takeover or game exit", flush=True)
                break
            observation = None
            if args.wram_file:
                observation = WRAM.load(args.wram_file).observe()
            elif args.wram_remote:
                # Fixed remote path; no model-generated command or file path.
                raw = subprocess.run(
                    ["ssh", "-o", "BatchMode=yes", "-o", "ConnectTimeout=7",
                     args.host, "cat /tmp/flxtr-som-wram.bin"],
                    capture_output=True, check=True, timeout=15).stdout
                observation = WRAM(raw).observe()
            action = choose_action(args.ollama_url, args.model,
                                   args.objective, previous, observation)
            guarded_action = guard_tile_action(action, observation)
            if guarded_action != action:
                print(f"[qwen] tile guard: {action} -> {guarded_action}", flush=True)
            action = guarded_action
            # The same action indefinitely is not intelligent control. Stop
            # instead of blindly attacking or walking into a wall.
            repeated = repeated + 1 if action == previous else 1
            if repeated >= 4:
                raise ValueError(f"Repeated action {action!r} four times; stopping")
            if history is not None and observation is not None:
                history.record(observation, action)
            step += 1
            total = "continuous" if args.steps == 0 else str(args.steps)
            print(f"[qwen] {step}/{total}: {action}", flush=True)
            if not args.dry_run:
                send_action(args.host, action)
            previous = action
            if args.steps == 0 or step < args.steps:
                time.sleep(args.interval)
        except (OSError, ValueError, subprocess.SubprocessError) as exc:
            print(f"[qwen] stopped safely: {exc}", file=sys.stderr)
            if args.ring_control:
                clear_remote_ring_ai(args.host)
            return 1
    if args.ring_control and args.steps != 0 and step >= args.steps:
        clear_remote_ring_ai(args.host)
    if history is not None:
        history.close()
    return 0

if __name__ == "__main__":
    sys.exit(main())

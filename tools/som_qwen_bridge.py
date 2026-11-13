#!/usr/bin/env python3
"""Persistent WRAM planner / optional Qwen -> FlXtR action bridge.

Runs on SkyLoft with a LOCAL Ollama instance. Qwen receives the last action,
the user-supplied objective, and optionally a read-only WRAM observation. The
default memory policy is deterministic and uses verified tiles, enemies,
weapon state and persistent map visits. Qwen remains an optional test policy.
"""
import argparse
import json
import re
import subprocess
import sys
import time
import urllib.request
from pathlib import Path

from som_memory import WRAM, ObservationHistory
from som_planner import MemoryPlanner

ACTIONS = {
    "step_right", "step_left", "step_up", "step_down", "wait",
    "attack", "open_ring", "confirm", "cancel", "next_ring",
    "prev_ring", "snapshot",
}
# Longest names first so "step_right" is not read as "right"-style prefixes.
_ACTION_PATTERN = re.compile(
    r"(?<![a-z_])(" + "|".join(sorted(ACTIONS, key=len, reverse=True)) + r")(?![a-z_])")
MAX_INVALID_REPLIES = 3
MAX_REPEATS = 4
# Reuse the SkyLoft-to-Steam-Link SSH transport. A fresh handshake for each
# marker check, WRAM read and action made one nominal one-second decision take
# four to five seconds on the live machines.
SSH_BASE = ["ssh", "-o", "BatchMode=yes", "-o", "ConnectTimeout=7",
            "-o", "ControlMaster=auto", "-o", "ControlPath=/tmp/flxtr-som-ssh-%C",
            "-o", "ControlPersist=120"]

STEP_OFFSETS = {
    "step_right": (1, 0), "step_left": (-1, 0),
    "step_up": (0, -1), "step_down": (0, 1),
}
OPPOSITE_STEP = {
    "step_right": "step_left", "step_left": "step_right",
    "step_up": "step_down", "step_down": "step_up",
}

def parse_action(raw: str):
    """Extract one allowlisted action from a small model's free-form reply.

    Small Qwen models often add punctuation, a prefix such as "Action:", or an
    empty <think></think> block even with think=False. Only names from the
    fixed allowlist are ever returned; anything else yields None.
    """
    text = re.sub(r"<think>.*?(</think>|$)", " ", raw or "", flags=re.S | re.I)
    text = text.lower().replace("-", "_")
    match = _ACTION_PATTERN.search(text)
    return match.group(1) if match else None

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
    candidate = parse_action(raw)
    if candidate is None:
        reason = result.get("done_reason", "unknown")
        raise ValueError(
            f"Qwen returned unsupported action {raw.strip()[:80]!r} "
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

def prefer_open_step(action: str, observation, previous: str) -> str:
    """Keep exploration visible when the model waits despite verified exits."""
    if action != "wait":
        return action
    context = observation.get("tile_context") if isinstance(observation, dict) else None
    if not isinstance(context, dict) or not context.get("runtime_verified") or context.get("game_mode") != "field":
        return action
    rows, radius = context.get("passability_rows"), context.get("radius")
    if not isinstance(rows, list) or not isinstance(radius, int):
        return action
    preferred = [previous] if previous in STEP_OFFSETS else []
    preferred += ["step_right", "step_down", "step_left", "step_up"]
    reverse = OPPOSITE_STEP.get(previous)
    candidates = []
    for step in preferred:
        if step not in candidates:
            candidates.append(step)
    candidates.sort(key=lambda step: step == reverse)
    for step in candidates:
        dx, dy = STEP_OFFSETS[step]
        row, column = radius + dy, radius + dx
        if (0 <= row < len(rows) and isinstance(rows[row], str) and
                0 <= column < len(rows[row]) and rows[row][column] == "."):
            return step
    return action

def send_action(host: str, action: str) -> None:
    # stdin carries an allowlisted command. No LLM text is interpolated into shell.
    cmd = [*SSH_BASE, host, "cat > /tmp/flxtr-som-ai-action"]
    subprocess.run(cmd, input=action + "\n", text=True, check=True, timeout=15)

def remote_ring_ai_enabled(host: str) -> bool:
    try:
        result = subprocess.run(
            [*SSH_BASE, host,
             "test -f /tmp/flxtr-som-ai-enabled"],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=15)
        return result.returncode == 0
    except (OSError, subprocess.SubprocessError):
        return False

def clear_remote_ring_ai(host: str) -> None:
    """End one ring-controlled session using only the fixed marker path."""
    try:
        subprocess.run(
            [*SSH_BASE, host,
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
    parser.add_argument("--policy", choices=("memory", "qwen"), default="memory",
                        help="memory uses persistent WRAM planning; qwen asks local Ollama")
    parser.add_argument("--objective", default="Explore slowly and attack occasionally")
    parser.add_argument("--steps", type=int, default=5)
    parser.add_argument("--interval", type=float, default=0.5)
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
    if not (0 <= args.steps <= 30) or args.interval < 0.25:
        parser.error("steps must be 1..30, or 0 for continuous ring control; interval minimum is 0.25")
    if args.steps == 0 and not args.ring_control:
        parser.error("steps=0 is allowed only with --ring-control")
    if (args.policy == "qwen" and
            args.ollama_url not in ("http://127.0.0.1:11434", "http://localhost:11434")):
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
    planner = MemoryPlanner(history)
    if args.ring_control:
        print(f"[ai] {args.policy} policy waiting for Action Grid confirmation", flush=True)
        deadline = time.monotonic() + args.start_timeout
        while not remote_ring_ai_enabled(args.host):
            if time.monotonic() >= deadline:
                print("[ai] stopped safely: Action Grid start timed out", file=sys.stderr)
                return 1
            time.sleep(1.0)
        print("[ai] Action Grid start received", flush=True)
    previous = "none"
    repeated = 0
    invalid_replies = 0
    step = 0
    while args.steps == 0 or step < args.steps:
        try:
            if args.ring_control and not remote_ring_ai_enabled(args.host):
                print("[ai] stopped by physical takeover or game exit", flush=True)
                break
            observation = None
            if args.wram_file:
                observation = WRAM.load(args.wram_file).observe()
            elif args.wram_remote:
                # Fixed remote path; no model-generated command or file path.
                raw = subprocess.run(
                    [*SSH_BASE, args.host, "cat /tmp/flxtr-som-wram.bin"],
                    capture_output=True, check=True, timeout=15).stdout
                observation = WRAM(raw).observe()
            if args.policy == "memory":
                decision = planner.decide(observation, previous)
                action = decision.action
                print(f"[ai] intent={decision.intention}: {decision.reason}", flush=True)
            else:
                try:
                    action = choose_action(args.ollama_url, args.model,
                                           args.objective, previous, observation)
                    invalid_replies = 0
                except ValueError as exc:
                    # One odd reply from a tiny model should not end the
                    # session. Persistent garbage still stops safely.
                    invalid_replies += 1
                    if invalid_replies >= MAX_INVALID_REPLIES:
                        raise
                    print(f"[ai] ignoring model reply "
                          f"({invalid_replies}/{MAX_INVALID_REPLIES}): {exc}", flush=True)
                    action = "wait"
            guarded_action = guard_tile_action(action, observation)
            if guarded_action != action:
                context = (observation or {}).get("tile_context") or {}
                why = context.get("verification_error") or (
                    "no WRAM observation" if observation is None else "next tile not open")
                print(f"[ai] tile guard: {action} -> {guarded_action} ({why})", flush=True)
            action = guarded_action
            if args.policy == "qwen":
                fallback_action = prefer_open_step(action, observation, previous)
                if fallback_action != action:
                    print(f"[ai] open-tile fallback: {action} -> {fallback_action}", flush=True)
                action = fallback_action
            # Long runs may intentionally repeat safe travel. Break repeated
            # attack/menu actions with one wait instead of ending the agent.
            repeated = repeated + 1 if action == previous else 1
            if (action not in STEP_OFFSETS and action != "wait" and
                    repeated >= MAX_REPEATS):
                print(f"[ai] repeated {action!r} {MAX_REPEATS} times; waiting once",
                      flush=True)
                action = "wait"
                repeated = 1
            if history is not None and observation is not None:
                history.record(observation, action)
            step += 1
            total = "continuous" if args.steps == 0 else str(args.steps)
            print(f"[ai] {step}/{total}: {action}", flush=True)
            if not args.dry_run:
                send_action(args.host, action)
            previous = action
            if args.steps == 0 or step < args.steps:
                time.sleep(args.interval)
        except (OSError, ValueError, subprocess.SubprocessError) as exc:
            print(f"[ai] stopped safely: {exc}", file=sys.stderr)
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

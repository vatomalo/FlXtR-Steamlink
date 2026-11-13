#!/usr/bin/env python3
"""Qwen-directed persistent WRAM planner -> FlXtR action bridge.

Runs on SkyLoft with a LOCAL Ollama instance. Qwen receives the last action,
the user-supplied objective, and optionally a read-only WRAM observation. The
default hybrid policy lets Qwen choose a durable intention while deterministic
code executes it using verified tiles, enemies, weapon state and map memory.
"""
import argparse
import json
import os
import re
import select
import queue
import subprocess
import sys
import threading
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
DIRECTIVES = {
    "explore_unknown", "explore_north", "explore_south",
    "explore_east", "explore_west", "engage_enemy", "avoid_enemy",
}
_DIRECTIVE_PATTERN = re.compile(
    r"(?<![a-z_])(" + "|".join(sorted(DIRECTIVES, key=len, reverse=True)) +
    r")(?![a-z_])")
MAP_NAMES = {128: "Waterfall", 131: "Potos Village", 179: "Prologue",
             256: "Opening room", 257: "House"}
DIRECTOR_TURNS = 12
# A directive is a few tokens. A small context window and output budget keep
# CPU prompt processing short; a stuck request is abandoned well before 90 s.
DIRECTOR_OPTIONS = {"temperature": 0.2, "num_predict": 12, "num_ctx": 1024}
DIRECTOR_TIMEOUT = 30
PROMPT_MEMORY_ITEMS = 4
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
SMOOTH_INTENTIONS = {"explore_frontier", "engage_enemy", "return_to_inn",
                     "survive", "replan"}
LONG_MOVES = {"step_right": "right", "step_left": "left",
              "step_up": "up", "step_down": "down"}
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

def parse_directive(raw: str):
    """Extract one allowlisted strategic directive from Qwen output."""
    text = re.sub(r"<think>.*?(</think>|$)", " ", raw or "", flags=re.S | re.I)
    text = text.lower().replace("-", "_")
    text = re.sub(r"\b(explore)\s+(unknown|north|south|east|west)\b", r"\1_\2", text)
    text = re.sub(r"\b(engage|avoid)\s+(enemy)\b", r"\1_\2", text)
    match = _DIRECTIVE_PATTERN.search(text)
    return match.group(1) if match else None

def allowed_directives(observation):
    choices = ["explore_unknown", "explore_north", "explore_south",
               "explore_east", "explore_west"]
    if not isinstance(observation, dict):
        return choices
    enemies = [enemy for enemy in observation.get("enemy_slots", [])
               if isinstance(enemy, dict) and enemy.get("active")]
    if enemies:
        hero = (observation.get("party") or [{}])[0]
        hp, max_hp = hero.get("hp", 0), hero.get("max_hp", 0)
        if max_hp and hp * 4 <= max_hp:
            return ["avoid_enemy", "engage_enemy", *choices]
        return ["engage_enemy", "avoid_enemy", *choices]
    return choices

def objective_directive(objective):
    """Seed an explicit travel mission immediately while Qwen thinks."""
    words = re.sub(r"[^a-z]+", " ", (objective or "").lower()).split()
    for direction in ("west", "east", "north", "south"):
        if direction in words:
            return f"explore_{direction}"
    return "explore_unknown"

def build_director_prompt(objective, observation, current, recent, known_cells,
                          combat_memory=None, place_memory=None):
    context = observation.get("tile_context", {}) if isinstance(observation, dict) else {}
    actor = context.get("actor", {})
    hero = (observation.get("party") or [{}])[0] if isinstance(observation, dict) else {}
    party = observation.get("party") or [] if isinstance(observation, dict) else []
    native_allies = [member for member in party
                     if member.get("availability_byte") == 1 and
                     member.get("controller_binding") == 0]
    enemies = [enemy for enemy in observation.get("enemy_slots", [])
               if isinstance(enemy, dict) and enemy.get("active")] if isinstance(observation, dict) else []
    choices = allowed_directives(observation)
    map_id = context.get("map_id")
    location = MAP_NAMES.get(map_id, f"unknown map {map_id}")
    return (
        "You are the strategic director for Secret of Mana. Do not choose buttons. "
        "Choose one durable intention for the safe navigation executor. "
        f"Player objective: {objective}. Location: {location}, map {map_id}, "
        f"tile {context.get('actor_tile')}, layer {actor.get('layer')}. "
        f"HP {hero.get('hp')}/{hero.get('max_hp')}; active enemies {len(enemies)}; "
        f"native AI allies {len(native_allies)}; "
        f"known map cells {known_cells}. Current directive: {current}. "
        f"Recent actions: {recent}. Allowed directives: {', '.join(choices)}. "
        f"Remembered places on this map: {compact(place_memory)}. "
        f"Learned combat outcomes: {compact(combat_memory)}. "
        "Prefer a consistent heading that reveals unknown territory. Engage a real enemy "
        "when healthy; avoid it when badly hurt. Reply with exactly one allowed directive."
    )

def compact(items):
    """Short JSON for prompt memory; prompt length dominates CPU latency."""
    return json.dumps(list(items or [])[:PROMPT_MEMORY_ITEMS], separators=(",", ":"))

def warm_model(url, model):
    """Load the model into memory before the first real directive."""
    payload = json.dumps({"model": model, "prompt": "", "keep_alive": "30m"}).encode()
    req = urllib.request.Request(url.rstrip("/") + "/api/generate", data=payload,
                                 headers={"Content-Type": "application/json"})
    try:
        started = time.monotonic()
        with urllib.request.urlopen(req, timeout=120) as response:
            response.read()
        print(f"[qwen-director] {model} loaded in {time.monotonic() - started:.1f}s",
              flush=True)
    except (OSError, ValueError) as exc:
        print(f"[qwen-director] could not preload {model}: {exc}", flush=True)

def choose_directive(url, model, objective, observation, current,
                     recent=None, known_cells=0, combat_memory=None, place_memory=None):
    choices = allowed_directives(observation)
    prompt = build_director_prompt(objective, observation, current,
                                   recent or [], known_cells, combat_memory, place_memory)
    payload = json.dumps({"model": model, "prompt": prompt, "stream": False,
                          "think": False, "keep_alive": "30m",
                          "options": DIRECTOR_OPTIONS}).encode()
    req = urllib.request.Request(url.rstrip("/") + "/api/generate", data=payload,
                                 headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=DIRECTOR_TIMEOUT) as response:
        result = json.load(response)
    raw = result.get("response") or ""
    directive = parse_directive(raw)
    if directive not in choices:
        raise ValueError(f"Qwen director returned {raw.strip()[:80]!r}; allowed={choices}")
    return directive

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

def extend_clear_exploration(action: str, observation, intention: str,
                             remaining_tiles=None) -> str:
    """Use a held direction when the full observed line is clear.

    The proxy lets each new command replace an unfinished hold, so re-sending
    the same hold every decision walks continuously and a changed decision
    steers immediately; the hold length only bounds a stalled planner.
    """
    if intention not in SMOOTH_INTENTIONS or action not in LONG_MOVES:
        return action
    # Brake for the last few tiles so a target is not overshot.
    if isinstance(remaining_tiles, int) and remaining_tiles < 3:
        return action
    context = observation.get("tile_context") if isinstance(observation, dict) else None
    if (not isinstance(context, dict) or not context.get("runtime_verified") or
            context.get("game_mode") != "field"):
        return action
    rows, radius = context.get("passability_rows"), context.get("radius")
    if not isinstance(rows, list) or not isinstance(radius, int) or radius < 2:
        return action
    dx, dy = STEP_OFFSETS[action]
    for distance in range(1, radius + 1):
        row, column = radius + dy * distance, radius + dx * distance
        if not (0 <= row < len(rows) and isinstance(rows[row], str) and
                0 <= column < len(rows[row]) and rows[row][column] == "."):
            return action
    return LONG_MOVES[action]

# One persistent POSIX-sh loop on the Steam Link. Each request is one line on
# stdin; no process or SSH channel is created per decision. The remote side
# accepts only lowercase action names and fixed paths, never model text.
REMOTE_LOOP = (
    "while IFS= read -r cmd; do "
    "case \"$cmd\" in "
    "obs|obsm) "
    "if [ \"$cmd\" = obsm ] && [ ! -f /tmp/flxtr-som-ai-enabled ]; then echo STOP; "
    "elif [ -f /tmp/flxtr-som-wram.bin ]; then echo WRAM; cat /tmp/flxtr-som-wram.bin; "
    "else echo NONE; fi;; "
    "act\\ *) a=${cmd#act }; "
    "case \"$a\" in ''|*[!a-z_]*) echo BAD;; "
    "*) printf '%s\\n' \"$a\" > /tmp/flxtr-som-ai-action && echo OK || echo BAD;; esac;; "
    "*) echo BAD;; esac; done"
)
WRAM_BYTES = 0x20000


class RemoteLink:
    """Keep one SSH session open for WRAM reads and actions."""

    def __init__(self, host: str, timeout: float = 5.0, remote_loop: str = REMOTE_LOOP,
                 ssh_command=None):
        self.host, self.timeout = host, timeout
        self.remote_loop = remote_loop
        self.ssh_command = list(ssh_command or SSH_BASE)
        self.process = None

    def _start(self):
        self.close()
        self.process = subprocess.Popen(
            [*self.ssh_command, "-o", "ServerAliveInterval=5", self.host, self.remote_loop],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
            bufsize=0)

    def close(self):
        process, self.process = self.process, None
        if process is not None:
            try:
                process.stdin.close()
            except OSError:
                pass
            try:
                process.wait(timeout=1)
            except subprocess.TimeoutExpired:
                process.kill()

    def _read_exact(self, size: int, deadline: float) -> bytes:
        chunks, remaining = [], size
        fd = self.process.stdout.fileno()
        while remaining:
            wait = deadline - time.monotonic()
            if wait <= 0 or not select.select([fd], [], [], wait)[0]:
                raise subprocess.TimeoutExpired("ssh link", self.timeout)
            chunk = os.read(fd, min(remaining, 65536))
            if not chunk:
                raise OSError("SSH link closed")
            chunks.append(chunk)
            remaining -= len(chunk)
        return b"".join(chunks)

    def _read_line(self, deadline: float) -> str:
        data = bytearray()
        while not data.endswith(b"\n"):
            data += self._read_exact(1, deadline)
            if len(data) > 16:
                raise OSError(f"unexpected SSH link reply {bytes(data)!r}")
        return data.decode("ascii", "replace").strip()

    def _request(self, line: str):
        for attempt in (0, 1):
            try:
                if self.process is None or self.process.poll() is not None:
                    self._start()
                deadline = time.monotonic() + self.timeout
                self.process.stdin.write(line.encode() + b"\n")
                status = self._read_line(deadline)
                payload = (self._read_exact(WRAM_BYTES, deadline)
                           if status == "WRAM" else None)
                return status, payload
            except (OSError, subprocess.SubprocessError):
                # The stream may be mid-frame; restart once, then fail safe.
                self.close()
                if attempt:
                    raise

    def fetch_wram(self, require_marker=False):
        status, payload = self._request("obsm" if require_marker else "obs")
        if status == "STOP":
            return None
        if status != "WRAM":
            raise OSError(f"WRAM snapshot unavailable ({status})")
        return payload

    def send_action(self, action: str) -> None:
        if action not in ACTIONS and action not in LONG_MOVES.values():
            raise ValueError(f"refusing non-allowlisted action {action!r}")
        status, _ = self._request("act " + action)
        if status != "OK":
            raise OSError(f"Steam Link rejected action {action!r} ({status})")


def send_action(host: str, action: str) -> None:
    # stdin carries an allowlisted command. No LLM text is interpolated into shell.
    cmd = [*SSH_BASE, host, "cat > /tmp/flxtr-som-ai-action"]
    subprocess.run(cmd, input=action + "\n", text=True, check=True, timeout=15)

def fetch_remote_wram(host: str, require_marker=False):
    """Read marker and WRAM in one SSH round trip; None means clean takeover."""
    remote = ("test -f /tmp/flxtr-som-ai-enabled || exit 44; "
              if require_marker else "") + "cat /tmp/flxtr-som-wram.bin"
    result = subprocess.run([*SSH_BASE, host, remote], capture_output=True, timeout=15)
    if require_marker and result.returncode == 44:
        return None
    if result.returncode:
        raise subprocess.CalledProcessError(result.returncode, result.args,
                                            output=result.stdout, stderr=result.stderr)
    return result.stdout

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
    parser.add_argument("--policy", choices=("hybrid", "memory", "qwen"), default="hybrid",
                        help="hybrid uses Qwen for intentions and WRAM planning for execution")
    parser.add_argument("--objective", default="Explore slowly and attack occasionally")
    parser.add_argument("--steps", type=int, default=5)
    parser.add_argument("--interval", type=float, default=0.25)
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
    if not (0 <= args.steps <= 30) or args.interval < 0.1:
        parser.error("steps must be 1..30, or 0 for continuous ring control; interval minimum is 0.1")
    if args.steps == 0 and not args.ring_control:
        parser.error("steps=0 is allowed only with --ring-control")
    if (args.policy in ("hybrid", "qwen") and
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
    link = RemoteLink(args.host) if not args.dry_run else None
    if args.policy == "hybrid":
        threading.Thread(target=warm_model, args=(args.ollama_url, args.model),
                         daemon=True, name="som-qwen-warmup").start()
    previous = "none"
    repeated = 0
    invalid_replies = 0
    directive = objective_directive(args.objective)
    directive_turns = 0
    directive_map = None
    director_results = queue.Queue()
    director_running = False
    last_sent_position = None
    unchanged_polls = 0
    step = 0
    while args.steps == 0 or step < args.steps:
        try:
            observation = None
            if args.wram_file:
                observation = WRAM.load(args.wram_file).observe()
            elif args.wram_remote:
                # Fixed remote path; no model-generated command or file path.
                raw = (link.fetch_wram(args.ring_control) if link else
                       fetch_remote_wram(args.host, args.ring_control))
                if raw is None:
                    print("[ai] stopped by physical takeover or game exit", flush=True)
                    break
                observation = WRAM(raw).observe()
            if history is not None and observation is not None:
                history.record_combat(observation, previous if previous != "none" else None)
            if args.policy in ("hybrid", "memory"):
                context = observation.get("tile_context", {}) if isinstance(observation, dict) else {}
                current_map = context.get("map_id")
                if args.policy == "hybrid":
                    try:
                        requested_map, selected, director_error = director_results.get_nowait()
                        director_running = False
                        if director_error is not None:
                            directive_turns = 3
                            directive_map = current_map
                            print(f"[qwen-director] unavailable; retaining {directive}: "
                                  f"{director_error}", flush=True)
                        elif requested_map == current_map:
                            directive = selected
                            directive_turns = DIRECTOR_TURNS
                            directive_map = current_map
                            print(f"[qwen-director] intention={directive} for "
                                  f"{DIRECTOR_TURNS} decisions", flush=True)
                        else:
                            print("[qwen-director] ignored stale decision after map change",
                                  flush=True)
                    except queue.Empty:
                        pass
                if (args.policy == "hybrid" and not director_running and
                        (directive_turns <= 0 or current_map != directive_map)):
                    recent = [item.get("action") for item in history.recent(6)] if history else []
                    known = 0
                    combat_memory = history.combat_summary() if history else []
                    place_memory = history.place_summary(current_map) if history else []
                    if history and context.get("actor"):
                        known = len(history.known_cells(current_map,
                                                        context["actor"].get("layer")))
                    # Qwen 3.5 can take tens of seconds to load on CPU. Never
                    # freeze controller output while the strategic director
                    # thinks; its result arrives through a thread-safe queue.
                    request = (current_map, observation, directive, recent, known,
                               combat_memory, place_memory)
                    def direct_in_background(request_data=request):
                        requested_map, observed, current, actions, cells, fights, places = request_data
                        try:
                            started = time.monotonic()
                            selected = choose_directive(
                                args.ollama_url, args.model, args.objective,
                                observed, current, actions, cells, fights, places)
                            print(f"[qwen-director] answered in "
                                  f"{time.monotonic() - started:.1f}s", flush=True)
                            director_results.put((requested_map, selected, None))
                        except (OSError, ValueError) as exc:
                            director_results.put((requested_map, None, str(exc)))
                    threading.Thread(target=direct_in_background, daemon=True,
                                     name="som-qwen-director").start()
                    director_running = True
                    directive_map = current_map
                    directive_turns = max(directive_turns, 3)
                    print(f"[qwen-director] thinking asynchronously; meanwhile {directive}",
                          flush=True)
                decision = planner.decide(observation, previous, directive)
                directive_turns -= 1
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
            sent_action = extend_clear_exploration(
                action, observation,
                decision.intention if args.policy in ("hybrid", "memory") else "qwen",
                decision.remaining_tiles if args.policy in ("hybrid", "memory") else None)
            if sent_action != action:
                print(f"[ai] smooth clear route: {action} -> {sent_action}", flush=True)
            actor = ((observation or {}).get("tile_context") or {}).get("actor") or {}
            current_position = ((observation or {}).get("map_id"),
                                actor.get("x_px"), actor.get("y_px"))
            movement = action in STEP_OFFSETS
            if movement and current_position == last_sent_position:
                unchanged_polls += 1
            else:
                unchanged_polls = 0
            await_progress = movement and unchanged_polls <= 2 and current_position == last_sent_position
            if await_progress:
                print("[ai] awaiting WRAM movement acknowledgement", flush=True)
            elif not args.dry_run:
                # After two unchanged snapshots, retry only the precise pulse;
                # never stack another long hold on stale position data.
                if movement and unchanged_polls > 2:
                    sent_action = action
                link.send_action(sent_action) if link else send_action(args.host, sent_action)
                if movement:
                    last_sent_position = current_position
            previous = action
            if args.steps == 0 or step < args.steps:
                time.sleep(args.interval)
        except (OSError, ValueError, subprocess.SubprocessError) as exc:
            print(f"[ai] stopped safely: {exc}", file=sys.stderr)
            if link:
                link.close()
            if args.ring_control:
                clear_remote_ring_ai(args.host)
            return 1
    if link:
        link.close()
    if args.ring_control and args.steps != 0 and step >= args.steps:
        clear_remote_ring_ai(args.host)
    if history is not None:
        history.close()
    return 0

if __name__ == "__main__":
    sys.exit(main())

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
import zlib
from pathlib import Path

from som_memory import (GP_REWARD, HP_PENALTY, WRAM, MapLoadGuard,
                        ObservationHistory)
from som_menu import (MAX_ITEM_COUNT, CandyMacro, Calibration, InnMacro, MacroFailed,
                      ShopMacro, candy_count, read_gold, ring_open)
from som_planner import MemoryPlanner, PlannerDecision
from som_personality import Personality, LINES
from som_learning import CombatLearner
from som_doors import DoorLearner
from som_dialogue import Commentary, NpcDialogue, qwen_comment
from som_walk import CurrentWalk, WalkFailed
from som_respawn import RespawnDialogue
from som_game_over import GameOverWatch
from som_event_escape import EventEscape
from som_play_loop import PlayLoop, SaveMacro
from som_block import BlockLearner
from som_tactics import TacticLearner
from som_build import classify as classify_build, profile as build_profile
from som_anticipate import AttackPredictor
from som_boss import BossRecorder
from som_movement import MovementAck
from som_rom import RomLayoutError, load_tables, scan_exit_tiles
from som_combat import ChargeFailed, ChargeMacro
from som_goals import (TRAVEL_TIMEOUT, build_goal_prompt, goal_plan, is_travel,
                       memory_summary, offered_goals, parse_goal)

ACTIONS = {
    "step_right", "step_left", "step_up", "step_down", "wait",
    "attack", "open_ring", "confirm", "cancel", "next_ring",
    "prev_ring", "snapshot", "menu_x", "charge", "block",
    "dash_right", "dash_left", "dash_up", "dash_down",
}
# Held for many seconds and ended by the bridge: never offered to the model.
CANDY_TRANSIENT = ("did not produce an open ring", "not in field mode")
CANDY_RETRY_S = 4.0
CANDY_MAX_RETRIES = 6
# Gambits: activity priorities 0..5 (0 = never) the player sets from the
# gamepad menu drawn by the proxy (src/som_gambit.h, same order and defaults).
GAMBIT_FIGHT, GAMBIT_INN, GAMBIT_SHOP, GAMBIT_SAVE, GAMBIT_CANDY, GAMBIT_TALK = range(6)
GAMBIT_DEFAULTS = "433341"
GAMBIT_NAMES = ("fight", "inn", "shop", "save", "candy", "talk")
GAMBIT_FILE = "/home/apps/retroarch/cores/experimental/som_ai_gambits"
GAMBIT_REFRESH = 5      # decisions between reads of the gambit mask
# Intentions the personality comments on (som_personality.LINES events).
SPOKEN_INTENTIONS = {"explore_frontier", "engage_enemy", "defend_enemy", "kite_enemy",
                     "evade_enemy", "cut_grass", "survive", "return_to_inn", "go_to_inn",
                     "go_to_shop", "go_to_map", "leave_room", "stuck"}
GUARD_REFRESHES = 2     # extra block presses per guard decision
GUARD_REFRESH_S = 0.2   # 12 frames, inside the proxy's 15-frame R hold
MODEL_ACTIONS = ACTIONS - {"charge", "block", "dash_right", "dash_left",
                           "dash_up", "dash_down"}
# Proxy commands for explicit tools only; never offered to the model.
CONTROL_COMMANDS = {"clone_enemy", "undo_spawn", "ai_on", "ai_off",
                    "summon_familiar", "dismiss_familiar", "summon_boss"}
FAMILIAR_FILE = "/tmp/flxtr-som-familiar"
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
HOLD_MIN_STRAIGHT = 4
SMOOTH_INTENTIONS = {"explore_frontier", "engage_enemy", "return_to_inn",
                     "survive", "replan", "go_to_shop"}
# follow_taught_path deliberately keeps short pulses on unverified tiles.
LONG_MOVES = {"step_right": "right", "step_left": "left",
              "step_up": "up", "step_down": "down"}
DASH_MIN_STRAIGHT = 6   # learned straight route tiles for a dash (live runs: mostly 5-7)
DASH_MIN_REMAINING = 8  # and the target at least this far away
DASH_ENEMY_PX = 160     # no dashing with a monster this close
DASH_MOVES = {"right": "dash_right", "left": "dash_left",
              "up": "dash_up", "down": "dash_down"}
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

def choose_goal(url, model, objective, summary, offered, current):
    """Ask Qwen for one goal from `offered`; ValueError on anything else."""
    prompt = build_goal_prompt(objective, summary, offered, current)
    payload = json.dumps({"model": model, "prompt": prompt, "stream": False,
                          "think": False, "keep_alive": "30m",
                          "options": DIRECTOR_OPTIONS}).encode()
    req = urllib.request.Request(url.rstrip("/") + "/api/generate", data=payload,
                                 headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=DIRECTOR_TIMEOUT) as response:
        result = json.load(response)
    raw = result.get("response") or ""
    goal = parse_goal(raw, offered)
    if goal is None:
        raise ValueError(f"Qwen returned {raw.strip()[:80]!r}; allowed={offered}")
    return goal

def choose_action(url: str, model: str, objective: str, previous: str, observation=None) -> str:
    prompt = (
        "You are a primitive test controller for Secret of Mana Turbo. "
        "You cannot see the screen, character location, inventory or enemy state. "
        "Choose ONE safe, short controller action from: "
        + ", ".join(sorted(MODEL_ACTIONS))
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

def guard_tile_action(action: str, observation, trusted=()) -> str:
    """Deterministically veto movement that lacks a verified open next tile.

    trusted: steps the memory planner has proven walkable (tiles the hero
    stood on, or conditional tiles matching a walked collision signature).
    """
    context = observation.get("tile_context") if isinstance(observation, dict) else None
    if isinstance(context, dict) and context.get("game_mode") != "field":
        return "wait" if action in STEP_OFFSETS or action == "attack" else action
    if action not in STEP_OFFSETS:
        return action
    if action in trusted:
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
                             remaining_tiles=None, straight_tiles=None) -> str:
    """Use a held direction when the full observed line is clear.

    The proxy lets each new command replace an unfinished hold, so re-sending
    the same hold every decision walks continuously and a changed decision
    steers immediately; the hold length only bounds a stalled planner.
    """
    if ((intention not in SMOOTH_INTENTIONS and not str(intention).startswith("go_to_"))
            or action not in LONG_MOVES):
        return action
    # A hold walks about four tiles. Without a straight run that long it
    # carries the hero past the route's next corner, the route then points
    # back, and the hero swings left-right around the turn.
    if not isinstance(straight_tiles, int) or straight_tiles < HOLD_MIN_STRAIGHT:
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
    attributes = context.get("attribute_rows") or []
    opposing = {"step_up":0x31, "step_right":0x32}.get(action)
    if opposing is not None and radius >= 5 and straight_tiles >= 3:
        lane = []
        for distance in range(1, 6):
            row, column = radius+dy*distance, radius+dx*distance
            if not (0 <= row < len(rows) and 0 <= column < len(rows[row]) and rows[row][column]=="."):
                break
            if row >= len(attributes) or column >= len(attributes[row].split()):
                break
            lane.append(int(attributes[row].split()[column][4:6],16))
        # Two consecutive opposing-current cells cannot be crossed by a
        # pulse followed by a long SSH idle. A verified plain hold made 30px
        # upstream on live Turbo; keep the five-cell lookahead as a bound.
        current = (context.get("actor") or {}).get("reported_ground_effect")
        if len(lane)==5 and (lane[:2]==[opposing,opposing] or
                              (current==opposing and lane[0]==opposing)):
            return LONG_MOVES[action]
    if (context.get("actor") or {}).get("reported_ground_effect") in (0x31, 0x32):
        return action
    for distance in range(1, radius + 1):
        row, column = radius + dy * distance, radius + dx * distance
        if not (0 <= row < len(rows) and isinstance(rows[row], str) and
                0 <= column < len(rows[row]) and rows[row][column] == "."):
            return action
        if row < len(attributes):
            encoded = attributes[row].split()
            if column < len(encoded):
                ground = int(encoded[column][4:6], 16)
                if ground in (0x31, 0x32) or (ground & 0xF0 == 0x10 and ground & 7 >= 4):
                    return action
    # A plain 30-frame walk covers 60px. Five verified tiles leave room for
    # the hero box as well as that displacement.
    if radius >= 5 and straight_tiles >= 5 and (remaining_tiles is None or remaining_tiles >= 5):
        # A dash goes further than the five tiles seen around the hero, so it
        # needs a long straight run on the learned route and a far target,
        # and never near a monster (travel, not a fight).
        if (intention != "engage_enemy" and straight_tiles >= DASH_MIN_STRAIGHT and
                (remaining_tiles is None or remaining_tiles >= DASH_MIN_REMAINING) and
                not enemy_within(observation, DASH_ENEMY_PX)):
            return DASH_MOVES[LONG_MOVES[action]]
        return LONG_MOVES[action]
    return action


def enemy_within(observation, limit):
    actor = ((observation or {}).get("tile_context") or {}).get("actor") or {}
    ax, ay = actor.get("x_px"), actor.get("y_px")
    if not isinstance(ax, int) or not isinstance(ay, int):
        return True                       # unknown: do not dash
    return any(isinstance(enemy, dict) and enemy.get("active") and
               isinstance(enemy.get("x_px"), int) and isinstance(enemy.get("y_px"), int) and
               abs(enemy["x_px"] - ax) + abs(enemy["y_px"] - ay) <= limit
               for enemy in (observation or {}).get("enemy_slots", []))

# One persistent POSIX-sh loop on the Steam Link. Each request is one line on
# stdin; no process or SSH channel is created per decision. The remote side
# accepts only lowercase action names and fixed paths, never model text.
# A failed redirect on the special builtin `exec` makes a POSIX shell exit,
# which would drop the session when the game is closed; `command exec` keeps
# the loop alive so it can answer NONE.
# WRAM that changes between decisions (measured live: system/state, dialogue
# and menus, inventory/gold/actors, 7F:8000). A hot read moves ~24 KiB instead
# of 128 KiB and is patched into the last full snapshot; a full snapshot is
# fetched every FULL_EVERY reads and whenever map, mode or ring state differ.
HOT_RANGES_KB = ((0, 8), (39, 2), (50, 10), (96, 4))
HOT_BYTES = sum(count for _, count in HOT_RANGES_KB) * 1024
FULL_EVERY = 6
# map id, game mode bytes and ring flag: a change means cold areas moved too
BASE_GUARD = (0xDC, 0xDD, 0xD0, 0xE4, 0xE6, 0x7D)
REMOTE_LOOP = (
    "while IFS= read -r cmd; do "
    "case \"$cmd\" in "
    "obs|obsm) "
    "if [ \"$cmd\" = obsm ] && [ ! -f /tmp/flxtr-som-ai-enabled ]; then echo STOP; "
    "elif [ -f /tmp/flxtr-som-wram.bin ]; then echo WRAM; cat /tmp/flxtr-som-wram.bin; "
    "else echo NONE; fi;; "
    "hotz|hotmz) "
    "if [ \"$cmd\" = hotmz ] && [ ! -f /tmp/flxtr-som-ai-enabled ]; then echo STOP; "
    "elif command exec 3< /tmp/flxtr-som-wram.bin 2>/dev/null; then echo WRAMH; "
    "{ " + "; ".join(f"dd if=/proc/self/fd/3 bs=1024 skip={start} count={count} 2>/dev/null"
                     for start, count in HOT_RANGES_KB) + "; } | gzip -1 -c; exec 3<&-; "
    "else echo NONE; fi;; "
    "gmb) m=; [ -f " + GAMBIT_FILE + " ] && read -r m < " + GAMBIT_FILE + "; "
    "case \"$m\" in [0-5][0-5][0-5][0-5][0-5][0-5]) ;; *) m=" + GAMBIT_DEFAULTS + ";; esac; "
    "echo \"GMB $m\";; "
    "fam) f=; [ -f " + FAMILIAR_FILE + " ] && read -r f < " + FAMILIAR_FILE + "; "
    "case \"$f\" in [1-3]) ;; *) f=0;; esac; echo \"FAM $f\";; "
    "obsz|obsmz) "
    "if [ \"$cmd\" = obsmz ] && [ ! -f /tmp/flxtr-som-ai-enabled ]; then echo STOP; "
    "elif command exec 3< /tmp/flxtr-som-wram.bin 2>/dev/null; then echo WRAMZ; gzip -1 -c <&3; exec 3<&-; "
    "else echo NONE; fi;; "
    "nav) if [ ! -f /tmp/flxtr-som-ai-enabled ]; then echo STOP; "
    "elif command exec 3< /tmp/flxtr-som-wram.bin 2>/dev/null; then echo NAV; "
    "dd if=/proc/self/fd/3 bs=256 count=1 2>/dev/null; "
    "dd if=/proc/self/fd/3 bs=256 skip=224 count=2 2>/dev/null; exec 3<&-; "
    "else echo NONE; fi;; "
    "act\\ *) a=${cmd#act }; "
    "case \"$a\" in ''|*[!a-z_]*) echo BAD;; "
    "*) printf '%s\\n' \"$a\" > /tmp/flxtr-som-ai-action && echo OK || echo BAD;; esac;; "
    r'msg\ *) t=${cmd#msg }; '
    r'case "$t" in ""|*[!A-Za-z0-9\ .,!?:/%+=\(\)\|\^-]*) echo BAD;; '
    r'*) printf "%s\n" "$t" > /tmp/flxtr-som-ai-message && echo OK || echo BAD;; esac;; '
    "*) echo BAD;; esac; done"
)
# Characters the Steam Link loop and the overlay font both accept.
MESSAGE_ALLOWED = re.compile(r"[^A-Za-z0-9 .,!?:/%+=()|^-]")
WRAM_BYTES = 0x20000


class RemoteLink:
    """Keep one SSH session open for WRAM reads and actions."""

    def __init__(self, host: str, timeout: float = 5.0, remote_loop: str = REMOTE_LOOP,
                 ssh_command=None, compress=True, partial=True):
        self.host, self.timeout = host, timeout
        self.remote_loop = remote_loop
        self.ssh_command = list(ssh_command or SSH_BASE)
        self.process = None
        self.compress = compress
        self.partial = partial and compress
        self.base, self.hot_reads = None, 0

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
                process.wait()
            process.stdout.close()

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

    def _read_gzip(self, deadline: float, size: int = WRAM_BYTES) -> bytes:
        """One gzip member from the stream; nothing follows it until the next request."""
        inflater = zlib.decompressobj(wbits=31)
        fd = self.process.stdout.fileno()
        out = []
        while not inflater.eof:
            wait = deadline - time.monotonic()
            if wait <= 0 or not select.select([fd], [], [], wait)[0]:
                raise subprocess.TimeoutExpired("ssh link", self.timeout)
            chunk = os.read(fd, 65536)
            if not chunk:
                raise OSError("SSH link closed")
            out.append(inflater.decompress(chunk))
        if inflater.unused_data:
            raise OSError("unexpected bytes after compressed WRAM")
        data = b"".join(out)
        if len(data) != size:
            raise OSError(f"compressed WRAM inflated to {len(data)} bytes")
        return data

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
                if status == "WRAMZ":
                    payload = self._read_gzip(deadline)
                if status == "WRAMH":
                    payload = self._read_gzip(deadline, HOT_BYTES)
                if status == "NAV":
                    payload = self._read_exact(768, deadline)
                return status, payload
            except (OSError, subprocess.SubprocessError):
                # The stream may be mid-frame; restart once, then fail safe.
                self.close()
                if attempt:
                    raise

    def fetch_wram(self, require_marker=False):
        # The Steam Link link carries ~210 KB/s; gzip -1 shrinks the 128 KiB
        # snapshot ~3x, and hot reads (HOT_RANGES_KB) cut a decision's read
        # from ~290 ms to ~160 ms.
        if self.partial and self.base is not None and self.hot_reads < FULL_EVERY:
            status, hot = self._request("hotmz" if require_marker else "hotz")
            if status == "STOP":
                return None
            if status == "WRAMH":
                image = bytearray(self.base)
                offset = 0
                for start, count in HOT_RANGES_KB:
                    image[start * 1024:(start + count) * 1024] = hot[offset:offset + count * 1024]
                    offset += count * 1024
                if all(image[i] == self.base[i] for i in BASE_GUARD):
                    self.hot_reads += 1
                    return bytes(image)
        command = "obsm" if require_marker else "obs"
        status, payload = self._request(command + "z" if self.compress else command)
        if status == "STOP":
            return None
        if status not in ("WRAM", "WRAMZ"):
            self.base = None
            raise OSError(f"WRAM snapshot unavailable ({status})")
        self.base, self.hot_reads = payload, 0
        return payload

    def fetch_gambits(self):
        """The gambit priorities (tuple of 0..5) the player set in the game."""
        status, _ = self._request("gmb")
        digits = status[4:] if status.startswith("GMB ") else ""
        if len(digits) != len(GAMBIT_NAMES) or any(c not in "012345" for c in digits):
            raise OSError(f"gambits unavailable ({status})")
        return tuple(int(c) for c in digits)

    def fetch_familiar(self):
        """Monster slot (1..3) of the hero's familiar, or 0 for none."""
        status, _ = self._request("fam")
        if status not in ("FAM 0", "FAM 1", "FAM 2", "FAM 3"):
            raise OSError(f"familiar unavailable ({status})")
        return int(status[4:])

    def show_message(self, text: str) -> None:
        """Ask the proxy to draw a short two-line message box ('|' = new line)."""
        clean = MESSAGE_ALLOWED.sub("", text.replace("_", " "))[:65].strip()
        if not clean:
            return
        status, _ = self._request("msg " + clean)
        if status != "OK":
            raise OSError(f"Steam Link rejected message ({status})")

    def fetch_navigation(self):
        status, raw = self._request("nav")
        if status == "STOP":
            return None
        if status != "NAV" or len(raw) != 768:
            raise OSError("navigation snapshot unavailable")
        low, hero = raw[:256], raw[256:]
        def word(data, offset):
            return data[offset] | data[offset+1] << 8
        hp, maximum = word(hero,0x182), word(hero,0x184)
        return {"tick":low[0xF4], "map":word(low,0xDC)&0x1FF,
                "x":word(hero,2),"y":word(hero,4),"hp":hp,"max_hp":maximum,
                "layer":hero[0xB]&0x80,"ground":hero[0x48],
                "valid":low[0xE4]==1 and low[0xE6]==0xF and low[0xD0]==0 and
                low[0x7D]!=1 and low[0xD9]==1 and hero[0]==1 and hero[0x2C]==1 and
                not word(hero,0x190)&0xC160 and 0 < hp <= maximum <= 999}

    def send_control(self, command: str) -> None:
        """Experimental proxy commands that only tools (never Qwen) may send."""
        if command not in CONTROL_COMMANDS:
            raise ValueError(f"refusing unknown control command {command!r}")
        status, _ = self._request("act " + command)
        if status != "OK":
            raise OSError(f"Steam Link rejected {command!r} ({status})")

    def send_action(self, action: str) -> None:
        if action not in ACTIONS and action not in LONG_MOVES.values():
            raise ValueError(f"refusing non-allowlisted action {action!r}")
        status, _ = self._request("act " + action)
        if status != "OK":
            raise OSError(f"Steam Link rejected action {action!r} ({status})")


INTENT_MESSAGES = {
    "explore_frontier": "AI: exploring",
    "engage_enemy": "AI: fighting",
    "return_to_inn": "AI: HP low|heading back to the inn",
    "recover_at_inn": "AI: at the inn",
    "survive": "AI: HP low|retreating",
    "stuck": "AI: stuck|no open tile",
    "evade_enemy": "AI: body flicker|afterimage",
    "go_to_shop": "AI: plenty of GP|going to buy Candy",
    "cut_grass": "AI: cutting grass",
}


LOOP_MESSAGES = {"hunt_here": "AI: hunting here|fighting monsters",
                 "go_to_shop": "AI: going shopping|buying Candy",
                 "go_to_inn": "AI: going to the inn|to rest",
                 "go_to_save": "AI: going to save",
                 "await_inn_setup": "AI: inn unavailable|return commitment held",
                 "await_save_setup": "AI: save unavailable|commitment held"}


def loop_message(goal):
    """Overlay text for a play-loop goal change."""
    if goal and goal.startswith("hunt_map_"):
        return f"AI: off to hunt|map {goal.rsplit('_', 1)[1]}"
    return LOOP_MESSAGES.get(goal, f"AI: {str(goal).replace('_', ' ')}")


def shop_errand(wram, observation, shop, gold, threshold, stock=MAX_ITEM_COUNT):
    """Shop for Candy when healthy, rich, and carrying fewer than `stock`."""
    if not isinstance(shop, dict) or not isinstance(gold, dict):
        return None
    hero = (observation.get("party") or [{}])[0]
    hp, max_hp = hero.get("hp", 0), hero.get("max_hp", 0)
    if not max_hp or hp * 2 <= max_hp or candy_count(wram) >= min(stock, MAX_ITEM_COUNT):
        return None
    money = read_gold(wram, gold)
    if money is None or money < max(threshold, int(shop.get("price", 0))):
        return None
    return {"place": "shop", "anchor": shop}


# "Eagle eye": the whole work map is already inside each WRAM snapshot, so
# wider scans cost no extra SSH traffic. Each decision still acts on the 7x7
# close-up; wider views only fill the learned map that routes are planned on.
SURVEY_EVERY = 10          # decisions between 33x33 scans
SURVEY_RADIUS = 16
MAP_SURVEY_RADIUS = 32     # 65x65 scan when a new map is entered


ROM_PATH = "/home/apps/retroarch/roms/Snes/Secret of Mana, Turbo.smc"
ROM_TABLE_CACHE = Path.home() / ".local/state/flxtr/som-rom-exits.json"


def load_rom_exits(host, cache=ROM_TABLE_CACHE):
    """Parsed ROM exit tables (read-only SSH reads), or None to fail closed."""
    def run(command, binary):
        result = subprocess.run([*SSH_BASE, host, command], capture_output=True,
                                check=True, timeout=60)
        return result.stdout if binary else result.stdout.decode()

    def digest():
        text = run(f'sha1sum "{ROM_PATH}"', False).split()
        return text[0] if text else None

    try:
        return load_tables(lambda: run(f'cat "{ROM_PATH}"', True), cache, digest)
    except (OSError, RomLayoutError, subprocess.SubprocessError) as exc:
        print(f"[rom] exit table unavailable, using learned crossings only: {exc}",
              flush=True)
        return None


MAX_CHARGE_FAILURES = 2


def weapon_note(weapon):
    """One log line for the equipped weapon, or why it is not trusted."""
    if not isinstance(weapon, dict):
        return None
    if not weapon.get("verified"):
        return "weapon fields not verified: " + ", ".join(weapon.get("failed_checks") or [])
    others = ", ".join(f"{name} {level}" for name, level in weapon["levels"].items()
                       if level and name != weapon["type"])
    return (f"{weapon['type']} tier {weapon['tier']} level {weapon['level']}" +
            (f" (also {others})" if others else ""))


def status_note(status):
    if not isinstance(status, dict):
        return None
    if not status.get("verified"):
        return f"status word {status.get('raw', 0):#06x} disagrees with HP; ignored"
    effects = ", ".join(status.get("effects") or []) or "normal"
    unknown = status.get("unknown_bits")
    return effects + (f" (+unknown bits {unknown:#06x})" if unknown else "") + \
        (", controls locked" if status.get("controls_locked") else "")


def survey(history, raw, radius):
    """Store a wide verified view in map memory; returns cells learned."""
    context = WRAM(raw).tile_context(radius=radius)
    if not context.get("runtime_verified"):
        return 0
    return history.remember_world({"tile_context": context})


def hide_familiar(observation, slot):
    """The hero's familiar is not an enemy: never target or fear it."""
    if not slot or not isinstance(observation, dict):
        return
    for enemy in observation.get("enemy_slots", []):
        if isinstance(enemy, dict) and enemy.get("slot") == slot:
            enemy["active"] = False
            enemy["familiar"] = True


def candy_fraction(weights):
    """HP fraction below which Candy is used: half when Candy weighs at
    least as much as resting at the inn, else only at critical HP."""
    return 0.5 if weights[GAMBIT_CANDY] >= weights[GAMBIT_INN] else 0.25


def apply_gambits(planner, play_loop, weights):
    """Weigh the activities against each other (see GAMBIT_DEFAULTS)."""
    fight, inn = weights[GAMBIT_FIGHT], weights[GAMBIT_INN]
    planner.peaceful = fight == 0
    planner.no_retreat = inn == 0
    # Fight over inn: rest later; inn over fight: rest earlier (8 % per step).
    planner.retreat_fraction = min(0.75, max(0.25, 0.5 + 0.08 * (inn - fight)))
    if play_loop is not None:
        play_loop.leave_fraction = min(0.85, max(0.3, 0.6 + 0.08 * (inn - fight)))


def candy_wanted(observation, fraction=0.5):
    """Use Candy below `fraction` of max HP when no enemy is within weapon reach."""
    if not isinstance(observation, dict) or not observation.get("candy_count"):
        return False
    hero = (observation.get("party") or [{}])[0]
    hp, max_hp = hero.get("hp", 0), hero.get("max_hp", 0)
    if not max_hp or hp > max_hp * fraction:
        return False
    actor = ((observation.get("tile_context") or {}).get("actor") or {})
    for enemy in observation.get("enemy_slots", []):
        if (enemy.get("active") and isinstance(actor.get("x_px"), int) and
                abs(enemy["x_px"] - actor["x_px"]) + abs(enemy["y_px"] - actor["y_px"]) < 64):
            return False
    return True


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
    parser.add_argument("--personality", choices=tuple(LINES), default="curious",
                        help="Non-modal AI dialogue style (default curious)")
    parser.add_argument("--no-personality", action="store_true",
                        help="Disable contextual AI speech bubbles")
    parser.add_argument("--play-loop", action="store_true",
                        help="deterministic hunt -> shop -> inn -> save cycle (som_play_loop.py)")
    parser.add_argument("--hunt-map", type=int,
                        help="play loop hunting map (default: best gold per HP from memory)")
    parser.add_argument("--trip-gold", type=int, default=100,
                        help="play loop: GP to earn per hunting trip")
    parser.add_argument("--no-anticipation", action="store_true",
                        help="never guard pre-emptively from learned enemy attack timing")
    parser.add_argument("--no-tactic-learning", action="store_true",
                        help="fight every enemy type the same way (no per-enemy press/kite/"
                             "close learning)")
    parser.add_argument("--block-learning", action="store_true",
                        help="guard with R after swings and when hit, learning per enemy "
                             "whether it pays, and counter after guarding (costs swings; "
                             "off by default)")
    parser.add_argument("--save-slot", type=int, choices=range(0, 5), default=1,
                        help="answer the innkeeper's after-stay save prompt with Yes "
                             "and save into this slot (1-4, the player's usual one); "
                             "0 never saves")
    parser.add_argument("--no-npc-talk", action="store_true",
                        help="Do not initiate conversations with nearby NPCs")
    parser.add_argument("--visit-map", type=int,
                        help="Travel to this map, then stay and talk to nearby NPCs")
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
    parser.add_argument("--hunt-direction", default="west",
                        choices=("west", "east", "north", "south", "none"),
                        help="Heading while healthy; Qwen may still choose engage/avoid")
    parser.add_argument("--shop-gold", type=int, default=500,
                        help="Buy Candy at the taught shop once gold reaches this")
    parser.add_argument("--gp-reward", type=float, default=GP_REWARD,
                        help="Reward points per GP gained")
    parser.add_argument("--hp-penalty", type=float, default=HP_PENALTY,
                        help="Penalty points per percent of max HP lost")
    parser.add_argument("--no-messages", action="store_true",
                        help="Do not draw AI status message boxes on the game screen")
    parser.add_argument("--ring-calibration", type=Path,
                        help="Ring calibration JSON from som_ring_probe.py "
                             "(default ~/.config/flxtr/som-ring.json)")
    parser.add_argument("--start-timeout", type=float, default=300.0,
                        help="Seconds to wait for Action Grid start when --ring-control is used")
    args = parser.parse_args()
    if args.visit_map is not None and not 0 <= args.visit_map <= 511:
        parser.error("visit-map must be 0..511")
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
    # Train only from live, takeover-guarded input. Offline dumps and sessions
    # without the physical-input marker cannot establish action attribution.
    learner = (CombatLearner(history.connection)
               if history is not None and args.wram_remote and args.ring_control and
               not args.dry_run else None)
    planner.combat_learner = learner
    map_guard = MapLoadGuard()
    doors = DoorLearner(history) if history is not None else None
    if history is not None and args.wram_remote and not args.dry_run:
        tables = load_rom_exits(args.host)
        if tables is not None:
            history.attach_rom(tables)
            confirmed, contradicted = history.rom_check_summary()
            state = ("trusted" if history.rom_exits_trusted() else
                     "NOT trusted (contradicted)" if contradicted else
                     "waiting for a confirming crossing")
            print(f"[rom] exit table of {len(tables.maps)} maps: {state} "
                  f"({confirmed} confirmed, {contradicted} contradicted crossings)",
                  flush=True)
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
    calibration = (Calibration.load(args.ring_calibration) if args.ring_calibration
                   else Calibration.load())
    candy_disabled = calibration is None
    candy_retry_at, candy_failures = 0.0, 0
    gambits = tuple(int(c) for c in GAMBIT_DEFAULTS)
    familiar_slot = 0
    apply_gambits(planner, None, gambits)       # the play loop follows on the first read

    def gambit(index):
        return gambits[index] > 0
    inn_disabled = False
    inn_lookalikes = set()
    surveyed_map, surveys_due = None, 0
    shop_disabled = False
    shop = history.get_agent_state("shop") if history is not None else None
    gold = history.get_agent_state("gold") if history is not None else None
    print("[shop] " + (f"Candy shop known, buying from {args.shop_gold} GP"
                       if isinstance(shop, dict) else
                       "no shop taught: run tools/som_teach_shop.py once"), flush=True)
    print("[candy] " + ("enabled from ring calibration" if calibration else
                        "disabled: run tools/som_ring_probe.py once to enable"), flush=True)
    messages = link is not None and not args.no_messages
    personality = (Personality(args.personality) if messages and
                   not args.no_personality else None)
    last_intention_spoken = None
    last_message = None

    def announce(text):
        nonlocal messages, last_message
        if not messages or text == last_message:
            return
        last_message = text
        try:
            link.show_message(text)
        except (OSError, subprocess.SubprocessError) as exc:
            messages = False
            print(f"[ai] message boxes disabled: {exc}", flush=True)
    def speak(event):
        if personality is None:
            return
        phrase = personality.speak(event)
        if phrase:
            # Prefix understood by som_overlay_set: screen-space speech bubble.
            # The proxy draws on a private video frame, never WRAM.
            announce("^" + phrase)
            print(f"[personality] {event}: {phrase}", flush=True)

    commentator = (Commentary(lambda text: qwen_comment(args.ollama_url, args.model, text))
                   if messages and args.policy == "hybrid" else None)
    npc_dialogue = (NpcDialogue(history, commentator)
                    if history is not None and args.wram_remote and not args.dry_run else None)

    if args.policy == "hybrid":
        threading.Thread(target=warm_model, args=(args.ollama_url, args.model),
                         daemon=True, name="som-qwen-warmup").start()
    previous = "none"
    repeated = 0
    invalid_replies = 0
    directive = objective_directive(args.objective)
    goal, goal_deadline = "hunt_here", 0
    directive_turns = 0
    directive_map = None
    director_results = queue.Queue()
    director_running = False
    movement_ack = MovementAck()
    reward_total = 0.0
    hero_facts = (None, None)
    charge_failures = 0
    orphan_ring_samples = 0
    orphan_ring_attempted = False
    respawn_dialogue = RespawnDialogue()
    game_over = GameOverWatch(history.connection if history is not None else None)
    event_escape = EventEscape(log=lambda line: print(line, flush=True))
    play_loop = None
    save_disabled = False
    last_loop_goal = None
    block_learner = None
    boss_recorder = BossRecorder(log=lambda line: print(line, flush=True)) if args.wram_remote else None
    # Off by default: on maps 129/130 at level 5-6 the hero dealt ~5x the
    # damage it took with no blocking (13-14 Nov afternoon), but only 0.4-1.6x
    # once it guarded after swings and when hit; guarding costs swings.
    planner.build = None              # log the hero's build once it is read
    tactic_learner = attack_predictor = None
    if not args.no_anticipation and history is not None and args.policy in ("hybrid", "memory"):
        attack_predictor = AttackPredictor(history.connection)
        planner.attack_predictor = attack_predictor
    if not args.no_tactic_learning and history is not None and args.policy in ("hybrid", "memory"):
        tactic_learner = TacticLearner(history.connection,
                                       log=lambda line: print(line, flush=True))
        planner.tactic_learner = tactic_learner
    if args.block_learning and history is not None and args.policy in ("hybrid", "memory"):
        block_learner = BlockLearner(history.connection, log=lambda line: print(line, flush=True))
        planner.block_learner = block_learner
    if args.play_loop and history is not None and args.policy in ("hybrid", "memory"):
        play_loop = PlayLoop(history, args.hunt_map, args.trip_gold,
                             log=lambda line: print(line, flush=True))
        planner.fight_all = True
        print(f"[loop] play loop on; phase {play_loop.phase}", flush=True)
    if history is not None:
        respawn_dialogue.restore_recent(history.connection)
        if respawn_dialogue.stage != "idle":
            print(f"[respawn] restored recent KO sequence: "
                  f"{respawn_dialogue.stage}", flush=True)
    if history is not None:
        print("[reward] " + ("+%g per GP gained, -%g per %% of max HP lost" %
                             (args.gp_reward, args.hp_penalty) if isinstance(gold, dict)
                             else "GP not calibrated (run tools/som_teach_shop.py); "
                             "only HP loss is penalized"), flush=True)
    step = 0
    while args.steps == 0 or step < args.steps:
        try:
            observation = None
            raw = None
            if args.wram_file:
                raw = args.wram_file.read_bytes()
                observation = WRAM(raw).observe(radius=5)
            elif args.wram_remote:
                # Fixed remote path; no model-generated command or file path.
                raw = (link.fetch_wram(args.ring_control) if link else
                       fetch_remote_wram(args.host, args.ring_control))
                if raw is None:
                    if learner is not None:
                        learner.cancel("physical takeover or game exit")
                    print("[ai] stopped by physical takeover or game exit", flush=True)
                    break
                observation = WRAM(raw).observe(radius=5)
            if observation is not None:
                if link and step % GAMBIT_REFRESH == 0:
                    try:
                        familiar_slot = link.fetch_familiar()
                    except (OSError, subprocess.SubprocessError):
                        pass
                hide_familiar(observation, familiar_slot)
                map_guard.apply(observation)
                if boss_recorder is not None and raw is not None:
                    try:
                        boss_recorder.observe(raw, WRAM(raw))
                    except OSError as exc:
                        print(f"[boss] recording failed: {exc}", flush=True)
                        boss_recorder = None
                if doors is not None:
                    doors.observe(WRAM(raw), observation)
                if npc_dialogue is not None:
                    npc_dialogue.observe(WRAM(raw), observation)
                    bubble = commentator.poll(observation["map_id"]) if commentator else None
                    if bubble:
                        announce(bubble)
                        print(f"[npc-comment] {bubble}", flush=True)
                if learner is not None:
                    learned = learner.observe(observation)
                    if learned is not None:
                        print(f"[learning] enemy={learned['enemy_id']:#04x} "
                              f"weapon={learned['weapon']} distance={learned['distance']}px "
                              f"damage={learned['damage']} hp_lost={learned['hp_lost']}",
                              flush=True)
                if personality is not None:
                    # Persistent memory, not just the previous map in this run.
                    # Check before the current observation is recorded/surveyed.
                    context_for_speech = observation.get("tile_context") or {}
                    map_for_speech = context_for_speech.get("map_id")
                    visited_before = False
                    if (history is not None and
                            context_for_speech.get("runtime_verified") and
                            isinstance(map_for_speech, int)):
                        visited_before = bool(history.connection.execute(
                            "SELECT 1 FROM tile_visits WHERE map_id=? LIMIT 1",
                            (map_for_speech,)).fetchone())
                    speech_event = personality.observation_event(
                        observation, visited_before=visited_before)
                    if speech_event:
                        speak(speech_event)
                # Game over -> title screen: record it and leave Continue to
                # the player. Nothing presses buttons outside a loaded game.
                death_event = game_over.observe(
                    observation, play_loop.phase if play_loop is not None else None)
                if death_event == "game_over":
                    if learner is not None:
                        learner.cancel("game over")
                    print(f"[death] game over: {game_over.describe()}; waiting on the "
                          "title screen for the player to Continue", flush=True)
                    announce(f"AI: died on map {game_over.last['map_id']}|press Continue")
                elif death_event == "resumed":
                    print("[death] game loaded again after the game over; resuming",
                          flush=True)
                    announce("AI: back in the game")
                hero_now = (observation.get("party") or [{}])[0]
                facts = (weapon_note(hero_now.get("weapon")), status_note(hero_now.get("status")))
                for label, before, after in zip(("weapon", "status"), hero_facts, facts):
                    if after != before and after is not None:
                        print(f"[{label}] {after}", flush=True)
                hero_facts = facts
            if npc_dialogue is not None and npc_dialogue.target is not None:
                npc_action = npc_dialogue.step(WRAM(raw), observation, planner)
                # NpcDialogue only advances in D0 0x83; Turbo often holds a
                # page in 0x84, which used to stall here for its 25 s timeout.
                if (npc_action is not None and npc_action[0] == "wait" and
                        npc_dialogue.phase == "dialogue" and npc_dialogue.target is not None):
                    escape = event_escape.observe(WRAM(raw))
                    if escape is not None:
                        npc_action = (escape[0], "NPC dialogue escape: " + escape[1])
                if npc_action is not None:
                    action, reason = npc_action
                    if learner is not None:
                        learner.cancel("NPC conversation")
                    link.send_action(action)
                    history.record(observation, action)
                    print(f"[npc] {reason}: {action}", flush=True)
                    previous = action
                    step += 1
                    time.sleep(args.interval)
                    continue
            if history is not None and observation is not None:
                history.record_combat(observation, previous if previous != "none" else None)
                money = read_gold(WRAM(raw), gold) if raw is not None else None
                reward = history.record_reward(observation, money, args.gp_reward,
                                               args.hp_penalty)
                if reward is not None:
                    reward_total += reward["score"]
                    enemy = (f"enemy type {reward['enemy_id']:#04x}"
                             if reward["enemy_id"] >= 0 else "no enemy near")
                    print(f"[reward] {reward['score']:+.1f} "
                          f"(+{reward['gold_gained']} GP, -{reward['hp_lost']} HP, map "
                          f"{reward['map_id']}, {enemy}); run total {reward_total:+.1f}",
                          flush=True)
                here_context = observation.get("tile_context") or {}
                here = here_context.get("map_id")
                surveys_due -= 1
                if (raw is not None and here_context.get("runtime_verified") and
                        here != surveyed_map and history.rom_tables is not None):
                    exits = scan_exit_tiles(WRAM(raw), here, history.rom_tables)
                    history.remember_exits(here, exits)
                    print(f"[rom] map {here}: {len(exits)} exit tiles to maps "
                          f"{sorted({item['to_map'] for item in exits})}", flush=True)
                if (raw is not None and here_context.get("runtime_verified") and
                        (here != surveyed_map or surveys_due <= 0)):
                    radius = MAP_SURVEY_RADIUS if here != surveyed_map else SURVEY_RADIUS
                    learned = survey(history, raw, radius)
                    if learned:
                        if here != surveyed_map:
                            print(f"[ai] eagle eye: mapped {learned} tiles of map {here}",
                                  flush=True)
                        surveyed_map, surveys_due = here, SURVEY_EVERY
            # Ordinary KO in Turbo auto-respawns the hero but can leave an
            # event/dialogue awaiting one or more B presses. Only a witnessed
            # dead -> transition -> alive/event sequence authorizes confirms.
            # Generic story events and shops remain untouched.
            if observation is not None and args.policy in ("hybrid", "memory"):
                event_byte = WRAM(raw).u8(0x7E00D0) if raw is not None else None
                recovery_action = respawn_dialogue.observe(observation, event_byte)
                if recovery_action is not None:
                    if learner is not None:
                        learner.cancel("KO recovery")
                    if link:
                        link.send_action(recovery_action)
                    elif not args.dry_run:
                        send_action(args.host, recovery_action)
                    print(f"[respawn] advancing verified KO dialogue "
                          f"({respawn_dialogue.confirms}/"
                          f"{respawn_dialogue.max_confirms})", flush=True)
                    previous = "wait"
                    continue
                if respawn_dialogue.finished and respawn_dialogue.confirms == respawn_dialogue.max_confirms:
                    if not getattr(respawn_dialogue, "_limit_logged", False):
                        respawn_dialogue._limit_logged = True
                        print("[respawn] dialogue did not clear after bounded confirms; "
                              "stopping automatic event input", flush=True)
                # Any other dialogue/event screen: the planner only acts in
                # field mode, so press through it with verified, bounded
                # inputs (som_event_escape.py) instead of waiting forever.
                # The respawn handler `continue`s when it presses, so it keeps
                # priority; waiting on it alone deadlocked on KO text pages.
                if raw is not None:
                    escape = event_escape.observe(WRAM(raw))
                    if escape is not None:
                        escape_action, reason = escape
                        if learner is not None:
                            learner.cancel("dialogue escape")
                        if link:
                            link.send_action(escape_action)
                        elif not args.dry_run:
                            send_action(args.host, escape_action)
                        print(f"[escape] {reason}: {escape_action}", flush=True)
                        previous = "wait"
                        continue
            # The service can restart with the Candy item ring still on screen.
            # In that state CandyMacro.ready() raises BEFORE its cleanup try:
            # the regular planner then waits forever for field mode.
            # Require three consecutive raw WRAM snapshots of a genuine item
            # ring before one bounded recovery attempt. Never touch events,
            # shops, ordinary dialogs or a non-ring mode.
            mode = ((observation or {}).get("tile_context") or {}).get("game_mode")
            observed_ring = (raw is not None and mode == "ring" and
                             ring_open(WRAM(raw)))
            if mode == "field":
                orphan_ring_samples = 0
                orphan_ring_attempted = False
            elif observed_ring and link and args.policy in ("hybrid", "memory"):
                orphan_ring_samples += 1
                if orphan_ring_samples >= 3 and not orphan_ring_attempted:
                    if learner is not None:
                        learner.cancel("ring recovery")
                    orphan_ring_attempted = True
                    print("[candy] orphan ring after restart/failure; "
                          "attempting verified Cancel cleanup", flush=True)
                    try:
                        if calibration is None:
                            raise MacroFailed("no ring calibration for Cancel cleanup")
                        CandyMacro(link, calibration)._close_ring()
                        print("[candy] orphan ring closed; resuming field play", flush=True)
                        previous = "wait"
                        continue
                    except (MacroFailed, OSError) as exc:
                        candy_disabled = True
                        print(f"[candy] orphan ring cleanup failed: {exc}; "
                              "manual menu exit required", flush=True)
                        announce("AI: ring stuck|close manually")
            else:
                orphan_ring_samples = 0
            if (link and not candy_disabled and args.policy in ("hybrid", "memory") and
                    time.monotonic() >= candy_retry_at and gambit(GAMBIT_CANDY) and
                    candy_wanted(observation, candy_fraction(gambits)) and mode == "field"):
                if learner is not None:
                    learner.cancel("Candy macro")
                announce("AI: HP low|using Candy")
                try:
                    gained = CandyMacro(link, calibration,
                                        log=lambda text: print(text, flush=True)).run()
                    print(f"[candy] used Candy: +{gained} HP", flush=True)
                    candy_failures = 0
                    announce(f"AI: Candy +{gained} HP")
                except MacroFailed as exc:
                    # The ring refusing to open (balloon, a swing or a hit in
                    # progress) is transient: retry soon instead of losing
                    # Candy for the run, which let the hero die at low HP.
                    # Anything that went wrong inside the menu still
                    # disables Candy rather than fumbling repeatedly.
                    transient = any(text in str(exc) for text in CANDY_TRANSIENT)
                    candy_failures = candy_failures + 1 if transient else CANDY_MAX_RETRIES
                    if candy_failures < CANDY_MAX_RETRIES:
                        candy_retry_at = time.monotonic() + CANDY_RETRY_S
                        print(f"[candy] retry in {CANDY_RETRY_S:.0f}s "
                              f"({candy_failures}/{CANDY_MAX_RETRIES}): {exc}", flush=True)
                    else:
                        candy_disabled = True
                        print(f"[candy] disabled for this run: {exc}", flush=True)
                        announce("AI: Candy failed|see log")
                    # Handle a failure before CandyMacro entered its own
                    # cleanup block (for example a stale ring-mode read).
                    if raw is not None and ring_open(WRAM(raw)):
                        orphan_ring_samples = 3
                previous = "wait"
                continue
            if args.policy in ("hybrid", "memory"):
                context = observation.get("tile_context", {}) if isinstance(observation, dict) else {}
                current_map = context.get("map_id")
                wram_now = WRAM(raw) if raw is not None else None
                money = read_gold(wram_now, gold) if wram_now is not None else None
                hero_stats = ((observation.get("party") or [{}])[0] or {}).get("stats") \
                    if isinstance(observation, dict) else None
                build = classify_build(hero_stats)
                if build != planner.build:
                    planner.build, planner.build_profile = build, build_profile(build)
                    stats_text = " ".join(f"{k[:3].upper()} {v}" for k, v in (hero_stats or {}).items())
                    print(f"[build] hero is a {build} ({stats_text}): fights "
                          f"{build_profile(build).get('preferred') or 'by learned tactics'}",
                          flush=True)
                if link and step % GAMBIT_REFRESH == 0:
                    try:
                        weights = link.fetch_gambits()
                    except (OSError, subprocess.SubprocessError):
                        weights = gambits
                    if weights != gambits:
                        print("[gambits] " + ", ".join(
                            f"{name} {weight}" for name, weight in zip(GAMBIT_NAMES, weights)),
                              flush=True)
                    gambits = weights
                    apply_gambits(planner, play_loop, gambits)
                shop_offer = (shop_errand(wram_now, observation, shop, gold, 0,
                                          gambits[GAMBIT_SHOP])
                              if wram_now is not None and not shop_disabled and
                              gambit(GAMBIT_SHOP) else None)
                summary = memory_summary(history, observation, money, shop_offer)

                def reachable(map_id, here=current_map):
                    return (history is not None and
                            history.map_path(here, map_id) is not None)
                offered = offered_goals(summary, args.hunt_direction, reachable)
                if args.policy == "hybrid":
                    try:
                        requested_map, selected, director_error = director_results.get_nowait()
                        director_running = False
                        if director_error is not None:
                            directive_turns = 3
                            directive_map = current_map
                            print(f"[qwen-director] unavailable; keeping goal {goal}: "
                                  f"{director_error}", flush=True)
                        elif requested_map != current_map or selected not in offered:
                            print(f"[qwen-director] ignored stale goal {selected}", flush=True)
                        else:
                            if selected != goal:
                                announce("Qwen goal: " + selected.replace("_", " "))
                            goal = selected
                            goal_deadline = step + TRAVEL_TIMEOUT
                            directive_turns = DIRECTOR_TURNS
                            directive_map = current_map
                            print(f"[qwen-director] goal={goal} (offered {', '.join(offered)})",
                                  flush=True)
                    except queue.Empty:
                        pass
                # A journey is kept until arrival or timeout; other goals are
                # reconsidered every DIRECTOR_TURNS decisions or on a new map.
                travelling = is_travel(goal) and step < goal_deadline
                if (args.policy == "hybrid" and play_loop is None and
                        not director_running and not travelling and
                        (directive_turns <= 0 or current_map != directive_map)):
                    # Qwen can take tens of seconds on CPU. Never freeze the
                    # controller; the goal arrives through a thread-safe queue.
                    request = (current_map, dict(summary), list(offered), goal)
                    def direct_in_background(request_data=request):
                        requested_map, facts, choices, current = request_data
                        try:
                            started = time.monotonic()
                            selected = choose_goal(args.ollama_url, args.model,
                                                   args.objective, facts, choices, current)
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
                    print(f"[qwen-director] thinking asynchronously; meanwhile goal {goal}",
                          flush=True)
                if goal not in offered and not is_travel(goal):
                    goal = "hunt_here"
                directive, goal_errand, done = goal_plan(
                    goal, summary, args.hunt_direction,
                    history.safe_anchor() if history is not None else None, shop_offer)
                if play_loop is None and (done or (is_travel(goal) and (step >= goal_deadline or
                                                 (goal_errand is None and not done)))):
                    print(f"[goal] {goal} {'reached' if done else 'ended'}; hunting here",
                          flush=True)
                    goal, directive_turns = "hunt_here", 0
                    directive, goal_errand, _ = goal_plan(goal, summary, args.hunt_direction)
                auto_errand = (shop_errand(wram_now, observation, shop, gold, args.shop_gold,
                                           gambits[GAMBIT_SHOP])
                               if wram_now is not None and not shop_disabled and
                               gambit(GAMBIT_SHOP) else None)
                if play_loop is not None:
                    # The loop owns goals: no Qwen goal and no automatic shop detour.
                    inn_anchor = history.safe_anchor()
                    save_point = history.get_agent_state("save")
                    loop_goal = play_loop.goal(
                        summary, shop_offer is not None,
                        isinstance(inn_anchor, dict) and bool(inn_anchor.get("face")) and
                        not inn_disabled and gambit(GAMBIT_INN),
                        isinstance(save_point, dict) and save_point.get("taught") and
                        not save_disabled and gambit(GAMBIT_SAVE), reachable)
                    planner.no_retreat = (play_loop.cornered(summary, reachable) or
                                          not gambit(GAMBIT_INN))
                    planner.stay_on_map = loop_goal == "hunt_here"
                    if loop_goal != last_loop_goal:
                        print(f"[loop] {play_loop.phase}: goal {loop_goal}", flush=True)
                        last_loop_goal = loop_goal
                        announce(loop_message(loop_goal))
                    goal = loop_goal
                    if goal in ("await_inn_setup", "await_save_setup"):
                        directive = "explore_unknown"
                        goal_errand = None
                    elif goal == "go_to_save":
                        directive = (f"explore_{args.hunt_direction}"
                                     if args.hunt_direction != "none" else "explore_unknown")
                        goal_errand = {"place": "save", "anchor": save_point,
                                       "arrive": "at_save"}
                    else:
                        directive, goal_errand, _ = goal_plan(
                            goal, summary, args.hunt_direction, inn_anchor, shop_offer)
                    auto_errand = None
                errand = goal_errand or auto_errand
                if args.visit_map is not None and current_map != args.visit_map:
                    errand = {"place": f"map {args.visit_map}", "anchor": {"map_id": args.visit_map},
                              "avoid_fights": True}
                elif args.visit_map == current_map:
                    errand = None
                decision = planner.decide(observation, previous, directive, errand)
                # Missing a taught inn/save macro must NOT freeze the hero.
                # The play-loop phase stays committed, but the deterministic
                # planner may explore, fight or evade while the interaction is
                # unavailable. The next observation retries the commitment.
                if (args.visit_map == current_map and decision.intention in
                        ("explore_frontier", "leave_room", "replan", "stuck")):
                    decision = PlannerDecision("wait", "at requested visit map", "visit_map")
                if (npc_dialogue is not None and not args.no_npc_talk and gambit(GAMBIT_TALK) and
                        (gambits[GAMBIT_TALK] > gambits[GAMBIT_FIGHT] or
                         not enemy_within(observation, DASH_ENEMY_PX)) and
                        decision.intention in ("explore_frontier", "leave_room", "replan", "stuck", "visit_map")):
                    npc_action = npc_dialogue.consider(WRAM(raw), observation, planner)
                    if npc_action is not None:
                        action, reason = npc_action
                        decision = PlannerDecision(action, reason, "talk_to_npc")
                if history is not None and context.get("map_id") not in inn_lookalikes:
                    score = history.looks_like("inn", observation)
                    if score is not None:
                        inn_lookalikes.add(context.get("map_id"))
                        history.remember_place(context["map_id"], context["actor"]["layer"],
                                               *context["actor_tile"], "inn_candidate",
                                               f"looks like the taught inn ({score:.0%})",
                                               "tile_pattern")
                        print(f"[inn] map {context['map_id']} looks like the taught inn "
                              f"({score:.0%} of tiles); teach it to use it", flush=True)
                        announce("AI: this looks like an inn|teach it to use it")
                if link and decision.action == "charge":
                    if learner is not None:
                        learner.cancel("charged attack macro")
                    try:
                        result = ChargeMacro(link, decision.charge_stage,
                                             decision.enemy_slot,
                                             log=lambda text: print(text, flush=True)).run()
                        if result["interrupted"]:
                            print(f"[charge] hit while charging (stage {result['stage']}); "
                                  "released", flush=True)
                        else:
                            print(f"[charge] stage {result['stage']} attack released; "
                                  f"enemy damage {result['damage']}", flush=True)
                            announce(f"AI: charged attack|stage {result['stage']}")
                    except ChargeFailed as exc:
                        charge_failures += 1
                        print(f"[charge] failed ({charge_failures}/{MAX_CHARGE_FAILURES}): "
                              f"{exc}", flush=True)
                        if charge_failures >= MAX_CHARGE_FAILURES:
                            planner.charge_disabled = True
                            print("[charge] disabled for this run; plain swings only",
                                  flush=True)
                    previous = "attack"
                    continue
                if link and errand and decision.intention == "at_shop":
                    if learner is not None:
                        learner.cancel("shop macro")
                    announce("AI: buying Candy")
                    cancel = calibration.cancel_action if calibration else "cancel"
                    try:
                        spent = ShopMacro(link, shop, gold, cancel=cancel,
                                          log=lambda text: print(text, flush=True)).run()
                        print(f"[shop] bought Candy for {spent} GP", flush=True)
                        announce(f"AI: bought Candy|-{spent} GP")
                    except MacroFailed as exc:
                        shop_disabled = True
                        print(f"[shop] disabled for this run: {exc}", flush=True)
                        announce("AI: shopping failed|see log")
                    previous = "wait"
                    continue
                if (link and not inn_disabled and history is not None and
                        decision.intention == "recover_at_inn"):
                    anchor = history.safe_anchor()
                    if isinstance(anchor, dict) and anchor.get("face"):
                        if learner is not None:
                            learner.cancel("inn macro")
                        announce("AI: staying at the inn")
                        cancel = calibration.cancel_action if calibration else "cancel"
                        try:
                            inn = InnMacro(link, anchor, cancel=cancel,
                                           log=lambda text: print(text, flush=True),
                                           save_slot=(args.save_slot or None)
                                           if gambit(GAMBIT_SAVE) else None)
                            gained = inn.run()
                            print(f"[inn] stayed at the inn: +{gained} HP", flush=True)
                            announce("AI: rested at the inn|HP restored")
                            if inn.saved:
                                announce(f"AI: game saved|slot {args.save_slot}")
                            if play_loop is not None:
                                play_loop.inn_done()
                                if inn.saved:
                                    play_loop.save_done()
                        except MacroFailed as exc:
                            if play_loop is not None:
                                play_loop.inn_failed(str(exc))
                            inn_disabled = True
                            print(f"[inn] disabled for this run: {exc}", flush=True)
                            announce("AI: inn failed|see log")
                        previous = "wait"
                        continue
                if (link and play_loop is not None and decision.intention == "at_save" and
                        isinstance(errand, dict) and errand.get("place") == "save"):
                    if learner is not None:
                        learner.cancel("save macro")
                    announce("AI: saving the game")
                    cancel = calibration.cancel_action if calibration else "cancel"
                    try:
                        pages = SaveMacro(link, errand["anchor"], cancel=cancel,
                                          log=lambda text: print(text, flush=True)).run()
                        print(f"[save] saved; pages {pages[-2:]}", flush=True)
                        announce("AI: game saved")
                        play_loop.save_done()
                    except MacroFailed as exc:
                        save_disabled = True
                        print(f"[save] disabled for this run: {exc}", flush=True)
                        announce("AI: save failed|see log")
                        play_loop.save_failed(str(exc))
                    previous = "wait"
                    continue
                directive_turns -= 1
                action = decision.action
                print(f"[ai] intent={decision.intention}: {decision.reason}", flush=True)
                if decision.reason.startswith("survival abandoned"):
                    announce("AI: inn did not heal|back to careful play")
                elif decision.intention in INTENT_MESSAGES and personality is None:
                    # Avoid re-issuing the generic status box every frame:
                    # it would immediately cover a timed speech bubble.
                    announce(INTENT_MESSAGES[decision.intention])
                spoken = ("go_to_map" if str(decision.intention).startswith("go_to_map")
                          else decision.intention)
                if spoken != last_intention_spoken and spoken in SPOKEN_INTENTIONS:
                    speak(spoken)
                    last_intention_spoken = spoken
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
            trusted = (planner.trusted_steps(observation)
                       if args.policy in ("hybrid", "memory") else ())
            guarded_action = guard_tile_action(action, observation, trusted)
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
            step += 1
            total = "continuous" if args.steps == 0 else str(args.steps)
            print(f"[ai] {step}/{total}: {action}", flush=True)
            sent_action = extend_clear_exploration(
                action, observation,
                decision.intention if args.policy in ("hybrid", "memory") else "qwen",
                decision.remaining_tiles if args.policy in ("hybrid", "memory") else None,
                decision.straight_tiles if args.policy in ("hybrid", "memory") else None)
            if sent_action != action:
                print(f"[ai] smooth clear route: {action} -> {sent_action} "
                      f"(straight {decision.straight_tiles}, remaining "
                      f"{decision.remaining_tiles}, monster near "
                      f"{enemy_within(observation, DASH_ENEMY_PX)})", flush=True)
            # A brief step on water or half-tiles can register only facing.
            # Do not change direction before WRAM confirms displacement.
            # Retry the SAME already-guarded step once, as a separate pulse.
            actual, movement_reason = movement_ack.decide(
                action, observation,
                decision.intention if args.policy in ("hybrid", "memory") else "qwen",
                allowed_steps={step for step in STEP_OFFSETS
                               if guard_tile_action(step, observation, trusted) == step})
            if actual is None:
                print(f"[movement] {movement_reason}", flush=True)
            elif not args.dry_run:
                if actual in STEP_OFFSETS and actual != action:
                    sent_action = actual
                if movement_reason.startswith("retry same"):
                    sent_action = actual  # no long hold on an uncertain tile
                    print(f"[movement] {movement_reason}: {actual}", flush=True)
                elif actual == "block":
                    sent_action = "block"
                context = (observation or {}).get("tile_context") or {}
                ground = (context.get("actor") or {}).get("reported_ground_effect")
                corridor = decision.navigation_path if args.policy in ("hybrid","memory") else None
                current_walk = link and ground in (0x31,0x32) and actual in STEP_OFFSETS and corridor
                if current_walk:
                    try:
                        actor = context["actor"]
                        progress = CurrentWalk(link,actual.removeprefix("step_"),
                            origin=(context["map_id"],actor["x_px"],actor["y_px"]),waypoints=corridor).run()
                        print(f"[movement] verified upstream walk: {progress}px",flush=True)
                    except WalkFailed as exc:
                        print(f"[movement] current walk stopped: {exc}",flush=True)
                else:
                    link.send_action(sent_action) if link else send_action(args.host, sent_action)
                    if sent_action == "block" and link:
                        # The proxy holds R for 15 frames but decisions are
                        # ~40 frames apart, so a single tap left the guard
                        # down most of the time. A new command replaces the
                        # unfinished hold: refresh it until the next decision.
                        for _ in range(GUARD_REFRESHES):
                            time.sleep(GUARD_REFRESH_S)
                            link.send_action("block")
                if learner is not None:
                    learner.action_sent(sent_action, observation)
                if block_learner is not None and observation is not None:
                    block_learner.record(observation, sent_action)
                if tactic_learner is not None and observation is not None:
                    tactic_learner.record(observation)
                if attack_predictor is not None and observation is not None:
                    attack_predictor.record(observation)
            if history is not None and observation is not None:
                history.record(observation, action if args.dry_run else
                               sent_action if actual is not None else None)
            previous = actual if actual is not None else "wait"
            if args.steps == 0 or step < args.steps:
                time.sleep(args.interval)
        except (OSError, ValueError, subprocess.SubprocessError) as exc:
            if learner is not None:
                learner.cancel("connection or observation failure")
            print(f"[ai] stopped safely: {exc}", file=sys.stderr)
            if link:
                link.close()
            if args.ring_control:
                clear_remote_ring_ai(args.host)
            return 1
    if learner is not None:
        learner.cancel("session ended")
    if link:
        link.close()
    if args.ring_control and args.steps != 0 and step >= args.steps:
        clear_remote_ring_ai(args.host)
    if history is not None:
        history.close()
    return 0

if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""Living world for Secret of Mana Turbo: schedules, off-screen simulation,
reconciliation with the live game and (opt-in) native NPC walks.

Layers (docs/SOM-LIVING-WORLD.md):
1. WorldClock (som_world_clock.py): deterministic, persistent game time.
2. Simulation (this file): every scheduled NPC has a persistent identity and
   an abstract state (location, activity, travel window, interruption, story
   override) advanced only at schedule boundaries: O(boundaries), no frames.
3. Reconciliation: when the NPC's map is loaded, the identity is bound to
   its live object (kind + map record) and the desired on-map place is
   compared with the real position.
4. NativeDriver: plans a collision-valid tile route and formats a native
   walk for the proxy. It is only *sent* with `run --live`; the proxy then
   refuses it unless its enable marker exists and its own checks pass.

Nothing here writes game memory directly, and no LLM output reaches the
proxy: an optional director may only pick among validated day overrides.

    python3 tools/som_living_world.py status
    python3 tools/som_living_world.py clock set 21:00
    python3 tools/som_living_world.py observe --wram-file wram.bin
    python3 tools/som_living_world.py run --host root@100.124.74.13     # read-only
    python3 tools/som_living_world.py run --host ... --live              # NPC walks
"""
import argparse
import json
import re
import sqlite3
import sys
import time
from pathlib import Path

import som_npc_native as native
from som_npc_schedule import DEFAULT_PATH, World
from som_world_clock import WorldClock, WorldTime, format_time

DEFAULT_DB = Path.home() / ".local/state/flxtr/som-living-world.sqlite"
STEAM_LINK = "root@100.124.74.13"
TILE_MINUTES = 0.05        # game minutes per tile walked (1 px/frame at 12x)
DOOR_MINUTES = 2.0         # game minutes to pass a door or change maps
TALK_MINUTES = 5           # a conversation holds the NPC this long
MAX_BLOCKS = 3             # refused steps from one tile before giving up there
GIVE_UP_CYCLES = 40        # driver pause after giving up
HERO_SLOTS = (0, 1, 2)
MESSAGE_ALLOWED = re.compile(r"[^A-Za-z0-9 .,!?:/%+=()|-]")
STORY_FLAGS = (0x10, 0x11, 0x12, 0x0D)

# Same shape as som_qwen_bridge.REMOTE_LOOP; only `obs`, `msg` and a strictly
# numeric `npc_walk` line are accepted on the Steam Link.
GAME_WAIT_S = 5.0   # retry interval while the game is closed

REMOTE_LOOP = (
    "while IFS= read -r cmd; do "
    "case \"$cmd\" in "
    "obs) if [ -f /tmp/flxtr-som-wram.bin ]; then echo WRAM; cat /tmp/flxtr-som-wram.bin; "
    "else echo NONE; fi;; "
    # RemoteLink asks for gzip-compressed snapshots by default.
    "obsz) if command exec 3< /tmp/flxtr-som-wram.bin 2>/dev/null; then echo WRAMZ; gzip -1 -c <&3; exec 3<&-; "
    "else echo NONE; fi;; "
    "npc\\ *) a=${cmd#npc }; "
    "case \"$a\" in 'npc_walk '*) b=${a#npc_walk }; "
    "case \"$b\" in ''|*[!0-9\\ ]*) echo BAD;; "
    "*) printf '%s\\n' \"$a\" > /tmp/flxtr-som-npc-command && echo OK || echo BAD;; esac;; "
    "*) echo BAD;; esac;; "
    r'msg\ *) t=${cmd#msg }; '
    r'case "$t" in ""|*[!A-Za-z0-9\ .,!?:/%+=\(\)\|-]*) echo BAD;; '
    r'*) printf "%s\n" "$t" > /tmp/flxtr-som-ai-message && echo OK || echo BAD;; esac;; '
    "*) echo BAD;; esac; done"
)


def travel_minutes(origin, target):
    """Lightweight off-screen travel estimate between two locations."""
    if origin is None or target is None or origin.name == target.name:
        return 0
    distance = abs(origin.x - target.x) + abs(origin.y - target.y)
    doors = 0 if origin.map == target.map else 1
    return max(1, round(distance * TILE_MINUTES + doors * DOOR_MINUTES))


class LivingWorld:
    """Persistent abstract state of every scheduled NPC."""

    def __init__(self, connection, world, clock):
        self.db, self.world, self.clock = connection, world, clock
        with connection:
            connection.executescript(
                "CREATE TABLE IF NOT EXISTS npc_state (npc_id TEXT PRIMARY KEY, state TEXT NOT NULL);"
                "CREATE TABLE IF NOT EXISTS npc_encounters (npc_id TEXT NOT NULL, minutes INTEGER "
                "NOT NULL, kind TEXT NOT NULL, detail TEXT);"
                "CREATE TABLE IF NOT EXISTS npc_day_overrides (npc_id TEXT NOT NULL, day INTEGER "
                "NOT NULL, minute INTEGER NOT NULL, destination TEXT NOT NULL, source TEXT, "
                "PRIMARY KEY (npc_id, day, minute));")

    # -- persistence -------------------------------------------------------
    def load_state(self, npc_id):
        row = self.db.execute("SELECT state FROM npc_state WHERE npc_id=?", (npc_id,)).fetchone()
        return json.loads(row[0]) if row else None

    def save_state(self, npc_id, state):
        with self.db:
            self.db.execute("INSERT OR REPLACE INTO npc_state VALUES (?,?)",
                            (npc_id, json.dumps(state, sort_keys=True)))

    def remember(self, npc_id, minutes, kind, detail=""):
        with self.db:
            self.db.execute("INSERT INTO npc_encounters VALUES (?,?,?,?)",
                            (npc_id, int(minutes), kind, detail))

    def encounters(self, npc_id, since_minutes=0, kind="talk"):
        return self.db.execute(
            "SELECT COUNT(*) FROM npc_encounters WHERE npc_id=? AND kind=? AND minutes>=?",
            (npc_id, kind, int(since_minutes))).fetchone()[0]

    # -- director (optional, validated) ------------------------------------
    def override_day(self, npc_id, day, minute, destination, source="director"):
        """Change one schedule slot for one day. Only an existing entry time and
        a destination the NPC already uses (or its home) are accepted."""
        npc = self.world.npcs[npc_id]
        if minute not in {entry.minute for entry in npc.entries}:
            raise ValueError("override must replace an existing schedule entry")
        allowed = {npc.resolve_name(entry.destination) for entry in npc.entries} | {npc.home}
        if destination not in allowed - {None}:
            raise ValueError(f"destination {destination!r} not allowed for {npc_id}")
        with self.db:
            self.db.execute("INSERT OR REPLACE INTO npc_day_overrides VALUES (?,?,?,?,?)",
                            (npc_id, int(day), int(minute), destination, source))

    def _destination(self, npc, entry, day):
        row = self.db.execute(
            "SELECT destination FROM npc_day_overrides WHERE npc_id=? AND day=? AND minute=?",
            (npc.id, day, entry.minute)).fetchone()
        return row[0] if row else npc.resolve_name(entry.destination)

    # -- simulation --------------------------------------------------------
    def _start_entry(self, npc, state, entry, moment):
        state["activity"] = entry.activity
        state["entry_minute"] = entry.minute
        target = self._destination(npc, entry, moment // 1440)
        if target is None or target == state.get("location") and not state.get("travel"):
            state["destination"] = state.get("location")
            return
        origin = self.world.location(state.get("location"))
        depart = max(moment, state.get("interrupted_until") or 0)
        state["destination"] = target
        state["travel"] = {"from": state.get("location"), "depart": depart,
                           "arrive": depart + travel_minutes(origin, self.world.location(target))}

    def advance(self, npc_id, now, flags=None):
        """Bring one NPC's abstract state to `now` (a WorldTime)."""
        npc = self.world.npcs[npc_id]
        state = self.load_state(npc_id)
        minutes = now.minutes
        if state is None:
            entry = npc.entry_at(now.minute_of_day)
            place = self._destination(npc, entry, now.day) or npc.home
            state = {"location": place, "destination": place, "activity": entry.activity,
                     "entry_minute": entry.minute, "travel": None, "interrupted_until": 0,
                     "override": None, "last_sim": minutes}
        if not npc.story_allows(flags or {}):
            state["override"] = "story_absent"
        elif state.get("override") == "story_absent":
            state["override"] = None
        for moment, entry in npc.boundaries(state["last_sim"], minutes):
            self._finish_travel(state, moment)
            self._start_entry(npc, state, entry, moment)
        self._finish_travel(state, minutes)
        state["last_sim"] = max(state["last_sim"], minutes)
        self.save_state(npc_id, state)
        return state

    @staticmethod
    def _finish_travel(state, minutes):
        travel = state.get("travel")
        if travel and minutes >= travel["arrive"]:
            state["location"], state["travel"] = state["destination"], None

    def advance_all(self, now, flags=None):
        return {npc_id: self.advance(npc_id, now, flags) for npc_id in self.world.npcs}

    def interrupt(self, npc_id, now, minutes=TALK_MINUTES, reason="talk"):
        """The player talked to (or startled) the NPC: hold it, remember it."""
        state = self.advance(npc_id, now)
        state["interrupted_until"] = now.minutes + minutes
        travel = state.get("travel")
        if travel and travel["depart"] >= now.minutes:
            shift = state["interrupted_until"] - travel["depart"]
            travel["depart"] += shift
            travel["arrive"] += shift
        self.save_state(npc_id, state)
        self.remember(npc_id, now.minutes, reason)
        return state

    # -- reconciliation ----------------------------------------------------
    def desired_place(self, npc_id, state, map_id):
        """Where the native NPC on map_id should stand now (Location) or None."""
        if state.get("override"):
            return None
        target = self.world.location(state.get("destination") or state.get("location"))
        return self.world.on_map(target, map_id)

    def context_line(self, npc_id, state, now):
        """Short overlay text for when the player talks to this NPC."""
        npc = self.world.npcs[npc_id]
        lines = npc.lines
        day_start = now.day * 1440
        if self.encounters(npc_id, day_start) >= 2 and "repeat" in lines:
            text = lines["repeat"]
        elif now.is_night and state.get("activity") != "sleep" and "night_warning" in lines:
            text = lines["night_warning"]
        else:
            text = lines.get(state.get("activity"), "")
        return MESSAGE_ALLOWED.sub("", text)[:65]


class NativeDriver:
    """Closed-loop native walks toward the desired place (one NPC)."""

    def __init__(self, world, npc_id):
        self.world, self.npc_id = world, npc_id
        self.npc = world.npcs[npc_id]
        self.blocked_edges = set()
        self.blocks = {}
        self.pending = None          # (tile, direction) of the last command
        self.pause = 0

    def observe_result(self, live):
        """Learn whether the previous walk moved the NPC."""
        if self.pending is None or live is None or live["busy"]:
            return
        (tile, x, y), direction = self.pending
        self.pending = None
        if (live["x"], live["y"]) == (x, y):
            self.blocked_edges.add((tile, direction))
            self.blocks[tile] = self.blocks.get(tile, 0) + 1
            if self.blocks[tile] >= MAX_BLOCKS:
                self.pause = GIVE_UP_CYCLES

    def plan(self, wram, place, map_id):
        """(command line or None, reason). Never writes anything."""
        if self.pause:
            self.pause -= 1
            if not self.pause:
                self.blocked_edges.clear()
                self.blocks.clear()
            return None, "gave up after repeated blocks; waiting"
        binding = self.npc.native
        if not binding or binding.get("map") != map_id:
            return None, "NPC not native to this map"
        npcs = native.decode_npcs(wram)
        live = native.bind(npcs, binding)
        if live is None:
            return None, "NPC not loaded"
        self.observe_result(live)
        if not live["static"]:
            return None, "NPC runs native wander AI; not driven"
        if place is None:
            return None, "no on-map place now"
        if live["tile"] == place.tile:
            return None, "at place"
        heroes = set()
        for slot in HERO_SLOTS:
            base = native.object_base(slot)
            if wram.u8(base) == 1:
                heroes.add(native.pixel_tile(wram.u16(base + 2), wram.u16(base + 4)))
        occupied = {npc["tile"] for npc in npcs if npc["slot"] != live["slot"]} | heroes
        x0 = min(live["tile"][0], place.x) - 6
        y0 = min(live["tile"][1], place.y) - 6
        grid = native.walkable_grid(wram, live["layer"], live["floor"],
                                    (x0, y0, max(live["tile"][0], place.x) + 6,
                                     max(live["tile"][1], place.y) + 6))
        route = native.plan_route(grid, live["tile"], place.tile, occupied,
                                  frozenset(self.blocked_edges))
        if route is None:
            return None, "no collision-valid route"
        direction, tiles = native.first_run(route)
        dx, dy = native.DIRECTIONS[direction][1]
        nxt = (live["tile"][0] + dx, live["tile"][1] + dy)
        if any(abs(nxt[0] - hx) + abs(nxt[1] - hy) <= 1 for hx, hy in heroes):
            return None, "yielding to the player"
        refusal = native.walk_refusal(wram, live["slot"], live["kind"], live["x"], live["y"],
                                      map_id)
        if refusal:
            return None, refusal
        frames = native.frames_for_tiles(tiles)
        self.pending = ((live["tile"], live["x"], live["y"]), direction)
        return native.walk_command(map_id, live, direction, frames), f"walk {direction} {tiles}"


def story_flags(wram):
    return {flag: wram.u8(0x7ECF00 + flag) for flag in STORY_FLAGS}


def talk_target(wram, world_state, world):
    """npc_id the player is talking to right now, or None."""
    if not wram.u8(0x7E00D0):
        return None
    target = wram.u16(native.TALK_TARGET)
    npcs = native.decode_npcs(wram)
    for npc_id, spec in world.npcs.items():
        live = native.bind(npcs, spec.native) if spec.native else None
        if live and native.object_base(live["slot"]) & 0xFFFF == target:
            return npc_id
    return None


def open_world(db_path=DEFAULT_DB, schedule=DEFAULT_PATH, real=None, rate=None):
    Path(db_path).parent.mkdir(parents=True, exist_ok=True)
    connection = sqlite3.connect(db_path)
    clock = WorldClock(connection, rate=rate, real=real)
    return LivingWorld(connection, World.load(schedule), clock)


def describe(world, now, map_id=None):
    out = [f"world time: {now.describe()} (rate {world.clock.rate * 60:.0f} game min per real min"
           f"{', paused' if world.clock.paused else ''})"]
    for npc_id, state in world.advance_all(now).items():
        travel = state.get("travel")
        where = state.get("location")
        if travel:
            where = (f"{travel['from']} -> {state['destination']} "
                     f"(arrive {format_time(travel['arrive'] % 1440)})")
        out.append(f"  {npc_id}: {state['activity']} at {where}"
                   f"{' [' + state['override'] + ']' if state.get('override') else ''}")
        if map_id is not None:
            place = world.desired_place(npc_id, state, map_id)
            out.append(f"    on map {map_id}: {place.name + ' ' + str(place.tile) if place else '-'}")
    return "\n".join(out)


def observe_once(world, wram, real=None, drivers=None):
    """One read-only reconciliation pass: returns report lines and commands."""
    from som_memory import WRAM  # noqa: F401  (type of wram)
    now = world.clock.now(real)
    map_id = wram.u16(native.MAP_ID) & 0x1FF
    flags = story_flags(wram)
    lines, commands = [], []
    drivers = drivers if drivers is not None else {}
    for npc_id, spec in world.world.npcs.items():
        state = world.advance(npc_id, now, flags)
        live = native.bind(native.decode_npcs(wram), spec.native) if spec.native else None
        place = world.desired_place(npc_id, state, map_id)
        driver = drivers.setdefault(npc_id, NativeDriver(world.world, npc_id))
        command, reason = driver.plan(wram, place, map_id)
        lines.append(f"{npc_id}: {state['activity']} -> "
                     f"{place.name + str(place.tile) if place else '-'}; live "
                     f"{'slot %d tile %s' % (live['slot'], live['tile']) if live else 'not loaded'}"
                     f"; {reason}")
        if command:
            commands.append(command)
    return now, lines, commands


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--db", default=str(DEFAULT_DB))
    parser.add_argument("--schedule", default=str(DEFAULT_PATH))
    parser.add_argument("--rate", type=float, help="game minutes per real second")
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("status")
    clock = sub.add_parser("clock")
    clock.add_argument("op", choices=("pause", "resume", "set", "rate"))
    clock.add_argument("value", nargs="?")
    observe = sub.add_parser("observe")
    observe.add_argument("--wram-file")
    observe.add_argument("--host", default=STEAM_LINK)
    run = sub.add_parser("run")
    run.add_argument("--host", default=STEAM_LINK)
    run.add_argument("--interval", type=float, default=0.5)
    run.add_argument("--live", action="store_true",
                     help="send native NPC walks (proxy enable marker still required)")
    run.add_argument("--no-messages", action="store_true")
    run.add_argument("--steps", type=int, default=0)
    run.add_argument("--director", action="store_true",
                     help="once per game day let Qwen pick one validated schedule change")
    run.add_argument("--ollama-url", default="http://127.0.0.1:11434")
    run.add_argument("--model", default="qwen3:0.6b")
    args = parser.parse_args(argv)
    real = time.time()
    world = open_world(args.db, args.schedule, real=real, rate=args.rate)
    if args.command == "status":
        print(describe(world, world.clock.now(real)))
    elif args.command == "clock":
        from som_world_clock import parse_time
        if args.op == "pause":
            world.clock.pause(real)
        elif args.op == "resume":
            world.clock.resume(real)
        elif args.op == "set":
            world.clock.set_time(parse_time(args.value), real)
        else:
            world.clock.set_rate(float(args.value), real)
        print(describe(world, world.clock.now(real)))
    elif args.command == "observe":
        from som_memory import WRAM
        if args.wram_file:
            wram = WRAM.load(args.wram_file)
        else:
            from som_qwen_bridge import RemoteLink
            link = RemoteLink(args.host, remote_loop=REMOTE_LOOP, partial=False)
            try:
                wram = WRAM(link.fetch_wram(False))
            finally:
                link.close()
        now, lines, commands = observe_once(world, wram, real)
        print(f"world time {now.describe()}, map {wram.u16(native.MAP_ID) & 0x1FF}, "
              f"mode {wram.game_mode()}")
        print("\n".join(lines))
        for command in commands:
            print(f"planned (not sent): {command}")
    else:
        return run_loop(world, args)
    return 0


def run_loop(world, args):
    from som_memory import WRAM
    from som_qwen_bridge import RemoteLink
    link = RemoteLink(args.host, remote_loop=REMOTE_LOOP, partial=False)
    drivers, last_talk, step = {}, None, 0
    daily = None
    if args.director:
        import som_npc_director as director
        daily = director.DailyDirector(world, director.qwen_chooser(args.ollama_url, args.model))
    print(f"[living-world] {'LIVE native NPC walks' if args.live else 'read-only'}; "
          f"world time {world.clock.now().describe()}", flush=True)
    try:
        waiting = False
        while not args.steps or step < args.steps:
            try:
                payload = link.fetch_wram(False)
            except OSError as exc:
                # Game closed or restarting: wait for it instead of exiting.
                if not waiting:
                    print(f"[living-world] waiting for the game ({exc})", flush=True)
                waiting = True
                time.sleep(GAME_WAIT_S)
                continue
            if waiting:
                print("[living-world] game is back", flush=True)
                waiting = False
            step += 1
            real = time.time()
            wram = WRAM(payload)
            world.clock.checkpoint(real)
            if daily is not None:
                for npc_id, choice in daily.tick(world.clock.now(real)):
                    print(f"[living-world] director {npc_id}: {choice}", flush=True)
            now, lines, commands = observe_once(world, wram, real, drivers)
            talking = talk_target(wram, None, world.world)
            if talking and talking != last_talk:
                state = world.interrupt(talking, now)
                line = world.context_line(talking, state, now)
                print(f"[living-world] talk {talking} at {now.describe()}: {line}", flush=True)
                if line and not args.no_messages:
                    link._request("msg " + line)
            last_talk = talking
            if step % 20 == 1:
                print("[living-world] " + now.describe() + " | " + " | ".join(lines), flush=True)
            for command in commands:
                if args.live:
                    status, _ = link._request("npc " + command)
                    print(f"[living-world] sent {command}: {status}", flush=True)
            time.sleep(args.interval)
    except KeyboardInterrupt:
        pass
    finally:
        link.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())

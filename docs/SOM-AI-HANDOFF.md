# Secret of Mana Turbo AI: handoff for a session running on SkyLoft

Branch `feature/som-ai-memory-agent-20261112`. Read `docs/SOM-WRAM-AGENT.md`
(feature-by-feature notes) and this file before changing anything.

## Machines

- **SkyLoft** (this machine): runs the AI bridge as the systemd user service
  `flxtr-som-ai.service` (`scripts/run-som-ai-service.sh`, settings in
  `~/.config/flxtr/som-ai.env`, memory database
  `~/.local/state/flxtr/som-ai.sqlite`), and local Ollama (Qwen).
- **Steam Link** `root@100.124.74.13` (Tailscale, passwordless BatchMode SSH):
  RetroArch with the opt-in proxy core
  `/home/apps/retroarch/cores/experimental/som_ai_proxy_libretro.so`, which
  wraps `snes9x2005_plus`. ROM `/home/apps/retroarch/roms/Snes/Secret of Mana, Turbo.smc`.
  The game is launched by the user from the FlXtR menu with core `som_ai_proxy`.
- **Android**: runs the same proxy core with app-private command files. FlXtR's
  embedded Tailscale listener exposes only the allowlisted SoM commands and
  exact 128-KiB WRAM snapshot to the selected SkyLoft peer. The SkyLoft
  service discovers the endpoint automatically through `tailscale status`
  and the peer-restricted `/.well-known/flxtr/som` route; no environment edit
  is required. `SOM_AI_HTTP_ENDPOINT` remains an optional manual override.
  The service runs independent Android and Steam Link planners at the same time.
  They share learned maps, routes, places, combat results and calibration in
  `som-ai.sqlite`, while current goals and live combat state are namespaced
  per device. Binary boss captures stay separate in `som-boss/` and
  `som-boss-android/`. When unset, only the Steam Link planner runs.

## Architecture

- `src/som_ai_proxy.c` (C, Libretro proxy on the Steam Link): injects P1 pad
  input from `/tmp/flxtr-som-ai-action` (allowlisted names, new command preempts
  an unfinished hold, physical input always cancels), exports WRAM to
  `/tmp/flxtr-som-wram.bin` every 6 frames, draws message boxes from
  `/tmp/flxtr-som-ai-message`, records physical presses while
  `/tmp/flxtr-som-ai-record` exists, applies verified in-memory ROM patches
  from `.../experimental/som_ai_rom_patch.txt` at load, and has the undoable
  `clone_enemy`/`undo_spawn` experiment (copies do not draw).
- `tools/som_qwen_bridge.py`: main loop. One persistent SSH link
  (`RemoteLink`: `obs`/`obsm`/`act`/`msg`), goal selection (Candy/inn when
  HP < 50 %, shop when rich, else hunt `SOM_AI_HUNT_DIRECTION`), tile guard,
  holds only on straight route runs, eagle-eye 33x33/65x65 map scans, async
  Qwen director (directive every 12 decisions; heading is locked by hunt mode).
- `tools/som_memory.py`: WRAM decoding (`WRAM`), tile rules from the real
  movement code, `ObservationHistory` SQLite memory (world cells with
  height-independent kinds, walked cells, passable/cuttable signatures, map
  edges, places, taught inn/shop, combat samples).
- `tools/som_planner.py`: deterministic planner. Height-aware BFS routes
  (`RouteMap` with straight-run length), frontier exploration, combat (route
  first, on-screen enemies only), survival/inn travel, errands, grass
  cutting, taught-path following, stall detection.
- `tools/som_menu.py`: closed-loop macros (Candy, inn, shop) that verify each
  press against WRAM and cancel out on anything unexpected.
- Living world (NPC schedules, world clock, opt-in native NPC walks):
  `tools/som_living_world.py`, see `docs/SOM-LIVING-WORLD.md`.
- Teach/probe tools: `som_ring_probe.py`, `som_teach_inn.py`,
  `som_teach_shop.py`, `som_teach_path.py`, `som_map_records.py`, `som_spawn.py`.

Reference for every memory address and rule: `cidwel/som-decomp` on GitHub
(`docs/maps.md`, `docs/hero-movement.md`, `docs/rabite.md`,
`docs/graphics.md`, `docs/monster-ai-opcodes.md`, ...). Turbo is a ROM hack of
the US ROM: verify layouts at runtime before trusting them.

## Commands

```sh
PYTHONPATH=tools python3 -m unittest discover -s tools -p 'test_som_*.py'
make test-som-ai-proxy test-som-ai-player2
bash scripts/build-som-ai-proxy.sh            # SDK auto-detected
scp build/som-ai/som_ai_proxy_libretro.so root@100.124.74.13:/home/apps/retroarch/cores/experimental/som_ai_proxy_libretro.so
systemctl --user restart flxtr-som-ai.service
journalctl --user -u flxtr-som-ai.service -n 80 --no-pager | grep -v awaiting
ssh root@100.124.74.13 'printf "ai_on\n" > /tmp/flxtr-som-ai-action'   # start AI
ssh root@100.124.74.13 'printf "ai_off\n" > /tmp/flxtr-som-ai-action'  # stop AI
```

Android normally needs no endpoint configuration. For troubleshooting only,
the copied endpoint can override automatic discovery:

```sh
printf '%s\n' 'SOM_AI_HTTP_ENDPOINT=http://ANDROID_TAILSCALE_IP:43927/TOKEN/som' >> ~/.config/flxtr/som-ai.env
systemctl --user restart flxtr-som-ai.service
```

Treat the endpoint as a password. The Android app accepts it only from the
SkyLoft Tailscale IPv4 address configured in its bridge screen.

Play loop (`tools/som_play_loop.py`, on by default in the service;
`SOM_AI_PLAY_LOOP=0` restores Qwen goals): hunt -> shop -> inn -> save ->
hunt. Hunt travels to the map with the best gold per HP lost in
`reward_ledger` (or `SOM_AI_HUNT_MAP`) and fights until `SOM_AI_TRIP_GOLD`
(100) GP, HP < 60 % or 3000 decisions; shop buys Candy; inn rests (skipped at
full HP); save replays the save taught with `tools/som_teach_save.py` and
requires the taught final page, else saving is skipped. Shop/inn/save give
up after 1500 decisions. Phase persists in agent_state `play_loop`.

Combat: when provoked (HP lost with an enemy within 96 px) the planner
fights back for 40 decisions: no low-HP retreat, no survival walk to the inn,
no backing away from unprofitable enemy types, no fight-skipping errands
(Candy still heals). With no learned route from the current map to the inn
(boss arena) the play loop fights instead of retreating. Guarding with R1
(`block`, `tools/som_block.py`) is learned per enemy type from HP lost per
close-range decision with and without it (table `block_stats`): explore until
8 samples each, then block only when it cuts damage by 20 %+.

Boss records (`tools/som_boss.py`, read-only): the bridge saves the
0x200-byte object and the full WRAM of every monster with max HP >= 200 the
first time it is seen (replaced once by a full-HP sighting) to
`~/.local/state/flxtr/som-boss/` with `index.json`. First entry: monster
0x57 (Potos story boss, map 260, max HP 300), captured at 0 HP while dying
on 2026-11-13. Summoning is not implemented: the existing `clone_enemy`
only copies a monster that is live on the current map; writing a recorded
object elsewhere without its graphics/palette/script loaded is unverified.

Dialogue escape (`tools/som_event_escape.py`): in any event screen the AI
did not start itself, once the screen has been unchanged for 1 s it presses
`confirm` on text pages (alternating with `cancel` if a page will not move),
only `cancel` on yes/no choices and other event states (so it never buys,
saves or accepts), at most one press per 0.6 s and 40 per event, then leaves
the screen to the player. KO-respawn and NPC-conversation handling keep
priority.

Idle auto-start: the proxy raises the same AI signal as `ai_on` after one
minute (3600 frames) without any physical button, once per idle period (a
new period starts with a real press; `ai_off` stays off until then; not while
the ring menu is open). Any press after a 30-frame grace hands control back.
Disable with
`ssh root@100.124.74.13 'touch /home/apps/retroarch/cores/experimental/som_ai_idle_autostart_disable'`.

Live inspection: `python3 tools/som_spawn.py show` prints the monster slots;
`WRAM(RemoteLink(host).fetch_wram()).observe()` gives the full observation.
Stop the service before running a teach or probe tool, and start it again after.

## Rules

- The user restarts Turbo after a proxy rebuild (copy only while the game is
  closed) and performs teaching walks; ask for that, do not work around it.
- Ask before anything that writes game RAM, patches ROM records, or could
  touch the user's save; tell the user to keep a backup save.
- Keep everything fail-closed: unverified state means wait, macros verify each
  step and cancel out, the model never sends raw commands (only allowlisted
  actions/directives; control commands are tool-only).
- Add a unit test for every behaviour change; run all tests before committing.
- Commit to this branch and push; never force-push.

## Open problems (latest first)

1. Bridge decks: verification fixed in `c13f10d` (pass-through tiles keep
   height/priority); confirm on hardware that the hero leaves the bridge.
2. Hold overshoot fixed in `65d4412`; confirm left/right swinging is gone.
3. Qwen `qwen3.5:4b` times out (30 s) on SkyLoft's CPU; `qwen3:0.6b` loads
   and answers faster.
4. Rabite spawning via `som_map_records.py` is untested on Turbo (the
   `$C8:7000` table may have moved).

## Next goals

1. Verify the fixes above live and tune from real observations.
2. Multi-map path finding: plan across maps with learned `map_edges` plus the
   ROM exit/transition tables from som-decomp `docs/maps.md`.
3. More decoded memory (gold address is calibrated per player; add chests,
   doors/exits, NPC records, weapon levels, status effects) with runtime
   cross-checks for Turbo.
4. Qwen as goal chooser: give it a compact memory summary (known maps, places,
   HP/MP, gold, inventory, explored fraction) and a small allowlist of goals
   (explore direction, hunt in map N, go to inn/shop, return to Potos);
   the planner executes them. Keep it asynchronous and optional.

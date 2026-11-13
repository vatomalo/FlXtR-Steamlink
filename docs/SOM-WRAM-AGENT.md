# Secret of Mana WRAM memory agent (experimental)

Branch: `feature/som-ai-memory-agent-20261112`.

The supervised service now defaults to `SOM_AI_POLICY=hybrid`. Qwen selects a
validated strategic directive every twelve decisions (heading, engage, or
avoid), while deterministic code validates the live field tile,
learns a persistent map/layer collision graph in SQLite, selects and retains a
frontier target, and follows a shortest known route until the intention is
reached or blocked. Combat overrides exploration: it can engage a verified
enemy, attack when the weapon is ready, or retreat below 25 percent HP. Set
Setting `SOM_AI_POLICY=memory` disables Qwen; `SOM_AI_POLICY=qwen` retains the older
unsafe one-model-call-per-button experiment.

The decision log names the current intention (`explore_frontier`,
`engage_enemy`, `survive`, `replan`, or `observe`) and its target/reason. SSH
connection reuse keeps the three remote operations per decision from paying a
new handshake each time.
Qwen inference runs on a daemon director thread: loading a larger CPU model can
take tens of seconds, but the safe planner continues acting on its previous
directive instead of freezing the player while it waits.
The live loop polls at 250 ms, combines marker validation and WRAM transfer in
one SSH round trip, and converts exploration steps to half-second holds only
when every tile across the verified three-tile lookahead is open. Combat and
uncertain routes retain short six-frame pulses.

The observation also reports each hero's controller binding and native Action
Grid behavior/cell/charge settings. Unbound companions already execute the
game's original 12 Hz hero AI for following, targeting, weapon positioning,
attacking, charging, and obstacle detours. Hybrid control will use that native
AI for recruited companions. The sole controller-bound leader remains under
the safe input bridge: directly clearing its binding is not sufficient because
the game must also initialize its AI bytecode program. Native behavior changes
must therefore go through the Action Grid/menu path rather than partial actor
WRAM writes.

Combat learning is observational and persistent. For every verified enemy
sample the history database stores map, enemy slot, previous action, distance,
hero/enemy HP, observed damage dealt and taken, and hero level. Aggregated
outcomes are included in later Qwen director prompts. This can teach selection
and timing among existing controller attacks; it cannot synthesize new ROM
attack or spell routines. Magic selection also requires a recruited Girl or
Sprite and a separately verified ring-menu macro.

## What works in this checkpoint

- Read **raw 131072-byte SNES WRAM**, starting at SNES address `7E0000`.
- Decode documented enemy slots, party attributes, and weapon-use flag.
- Decode the hero's pixel position, current map, active 16×16 tile layer, and
  a compact 7×7 nearby tile/collision grid.
- Use verified WRAM observations directly in the default planner; optionally
  provide the same compact observation to local Ollama.
- Export a live, read-only 128-KiB WRAM image atomically from the opt-in
  Libretro proxy every 30 emulated frames.
- Persist observations, actions, and map-scoped tile visits in SQLite.
- Stop after 4 repeated attack/menu actions while allowing intentional travel.
- Preserve the existing SSH allowlisted action protocol.

Tile mapping fails closed: the grid is published as runtime-verified only when
the tile under the hero agrees with the actor's independently maintained floor,
ground-effect, and priority fields. Turbo may leave the actor's tile-ID cache
stale, so that byte is retained as a warning rather than a movement veto.
Exploration is local rather than a full
map route solver: it chooses among verified adjacent tiles using persisted visit
counts and enemy direction. Party/item/enemy spawning is intentionally not
exposed: each requires a different native game routine, and raw partial actor
writes can corrupt the current map or save. Spawning needs a separately gated,
reversible implementation before the planner may request it.

## Run tests

```sh
PYTHONPATH=tools python3 -m unittest discover -s tools -p 'test_som_*.py' -v
python3 -m py_compile tools/som_memory.py tools/som_planner.py tools/som_qwen_bridge.py
```

## Use an existing raw WRAM dump

```sh
python3 tools/som_qwen_bridge.py \
  --wram-file /path/to/wram.bin \
  --history-db /tmp/som-observations.sqlite \
  --model qwen3:0.6b --steps 5 --dry-run
```

A dump is reread once per step. A static file is **not live perception**.
When the opt-in `som_ai_proxy` core is running the exact Turbo filename, it
copies exactly 128 KiB from `retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM)`
to `/tmp/flxtr-som-wram.bin` every 30 emulated frames. Publication uses a
temporary file plus atomic rename, and unload removes the snapshot so the
bridge does not consume stale state. Use `--wram-remote` to read it via SSH.

This makes the bytes live; it does **not** make unverified fields trustworthy.
The observation now includes nearby tile IDs and a compact passability grid.
Solid cells are marked `#`, open cells `.`, conditional slopes/layer switches
and moving ground `?`, and the actor `@`. Runtime cross-checking protects the
AI from consuming a shifted or incompatible Turbo layout, but real hardware
testing is still required before this grid drives autonomous movement.

Qwen uses six-frame `step_*` movement commands rather than the older 30-frame
manual movement pulses. A deterministic guard changes a proposed step to
`wait` unless the runtime-verified adjacent cell is open. Blocked (`#`),
conditional (`?`), malformed, and unverified grids all fail closed.
The supervised profile requests a new decision every 0.5 seconds. Safe repeated
steps are allowed, and a model-proposed `wait` becomes a deterministic open
step only when the runtime-verified grid has an adjacent open cell. Repeated
attacks or menu actions still stop the session.

## Troubleshooting: AI connects but Randi never moves

- Rebuild and reinstall `som_ai_proxy_libretro.so` after commit "Inject AI
  input on bitmask joypad queries". Earlier builds only injected per-button
  queries, but snes9x2005 reads the whole pad with one
  `RETRO_DEVICE_ID_JOYPAD_MASK` query whenever RetroArch supports bitmasks,
  so every injected action was silently dropped (`COMPLETE ... button
  sampled 0 times` in the game log).
- `[qwen] tile guard: step_x -> wait (actor_tile_fields_disagree)` means the
  Turbo tile layout did not pass runtime verification, so movement is vetoed
  by design. Waiting no longer ends the session; only attacks can occur.
- Odd replies from small models (`Step_Right.`, `<think></think>wait`) are
  now parsed; up to two unparseable replies in a row are treated as `wait`.

## On-screen AI messages

The proxy can draw a two-line message box (32 characters per line) over the
game picture. It draws on a private copy of each frame; game RAM, ROM, and
the core's own frame buffer are untouched. The bridge shows a message when
the AI's intention or Qwen directive changes; disable with `--no-messages`.
Send one by hand (allowed: letters, digits, space and `.,!?:/%+=()-`; `|`
starts line two):

```sh
ssh root@100.124.74.13 'printf "Hello from SkyLoft|second line\n" > /tmp/flxtr-som-ai-message'
```

This is not the game's native dialog window. Making the game's own text
engine show new text would need a WRAM/ROM write path plus the Turbo text
script format, which this read-only proxy deliberately does not have.

## Candy (ring-menu macro)

Below half HP, with no enemy within 64 px and Candy in the inventory, the
bridge opens the item ring, rotates to Candy, confirms item and target, and
requires an observed HP gain or Candy count drop within three seconds. Every
press waits for the matching WRAM change; anything unexpected presses Cancel
until the ring is closed and disables Candy for the rest of the run.

The ring cursor byte is not documented for Turbo, so calibrate once:

```sh
systemctl --user stop flxtr-som-ai.service
python3 tools/som_ring_probe.py
systemctl --user start flxtr-som-ai.service
```

The probe asks you to open the item ring and rotate it a few times, finds the
byte that follows the cursor, records the Candy position, and tests whether
Y (`open_ring`) or X (`menu_x`) opens the ring. Recalibrate if Candy changes
inventory slot. Until calibrated, the log says `[candy] disabled`.

**Inn (next):** reuse the same closed-loop pattern: walk to the learned inn
tile, face the innkeeper, Confirm to talk, confirm "Yes" and payment, then
require full HP. The dialog/choice bytes still need the same kind of probe.

## Responsiveness (continuous control)

- The bridge keeps **one** SSH session open to a tiny allowlisted shell loop on
  the Steam Link (`obs`, `obsm`, `act <name>`), instead of starting two SSH
  sessions per decision. Action names are checked on both ends.
- The proxy exports WRAM every 6 frames (10 Hz, no fsync) and reads the action
  file every 2 frames. A new command **replaces** an unfinished hold, so the
  bridge re-sends half-second holds each decision for continuous walking and
  can steer or stop (`wait`) immediately. Physical input still cancels.
- Holds are used for every movement intention whenever three tiles ahead are
  verified open, braking to six-frame pulses in the last two tiles.
- Default decision interval is 0.1 s; the history database uses SQLite WAL.
- The Qwen director preloads the model at start, logs how long each answer
  took, uses a 1024-token context, 12 output tokens, a compact prompt, and a
  30 s timeout. It never blocks movement.

The proxy changes require rebuilding and reinstalling `som_ai_proxy_libretro.so`.

## Planner behaviour notes (live log findings)

- An enemy behind a wall is approached along the learned map. If no known
  route gets closer, it is ignored for 20 decisions instead of alternating
  `engage_enemy` (step toward the wall) with `explore_frontier` (walk away).
- While evading at low HP, the current escape direction is kept unless another
  open step is at least 16 px safer, preventing left/right/left jitter.
- `intent=observe` now names the verification failure, e.g.
  `(actor_tile_fields_disagree, mode=field)` or `(not_field_mode, mode=event)`.
- Survival mode is stored in the history database and survives service
  restarts. It ends when HP is back to 90 percent, after 30 decisions on the
  learned inn spot without HP rising (paying the innkeeper is not automated),
  or after 900 decisions without recovery. After a give-up only a new critical
  (25 percent) HP drop restarts the retreat.
- Map crossings are stored permanently in `map_edges` (both directions), so
  the way home is not lost when the 5000-row observation log rolls over, and
  the return route can chain several maps. Older databases are backfilled
  once from their remaining observation log.

## Start from the existing Action Grid ring menu

Run the SkyLoft bridge with `--ring-control --wram-remote`, then open the
game's existing Action Grid, open its 4×4 selector, and confirm the currently
selected square without changing it. The proxy observes that harmless commit
transition and publishes `/tmp/flxtr-som-ai-enabled`; it does not alter the
ring, personality value, ROM, or WRAM. Closing the ring releases the start
signal. Any physical controller input after the short menu-close grace period,
game unload, or core shutdown removes the signal and the bridge stops.

```sh
python3 tools/som_qwen_bridge.py --ring-control --wram-remote \
  --model qwen3:0.6b --steps 30 --objective "Follow open tiles and explore safely"
```

The bridge process must already be running (or supervised as a SkyLoft
service). The Steam Link ring cannot itself launch a process on SkyLoft.

## Keep the SkyLoft bridge running

Install the provided user service from the AI checkout as the normal SkyLoft
user (not with `sudo`):

```sh
git switch feature/som-ai-memory-agent-20261112
git pull --ff-only
bash scripts/install-som-ai-service.sh
sudo loginctl enable-linger "$USER"
```

The service waits without controlling the game until the Action Grid gesture
creates the marker. It then runs continuously (`--steps 0`) until physical
takeover, game exit, or a safety failure clears the marker. `Restart=always`
restarts the waiting bridge after a process crash, but a safety stop cannot
resume movement because the marker is removed first.

Configuration is kept outside Git at `~/.config/flxtr/som-ai.env`. After
changing the model or objective, restart with:

```sh
systemctl --user restart flxtr-som-ai.service
journalctl --user -u flxtr-som-ai.service -f
```

The service needs passwordless BatchMode SSH to the Steam Link and the selected
Ollama model already installed locally. Check those independently with
`ssh -o BatchMode=yes root@100.124.74.13 true` and `ollama list`.

If the Action Grid is unavailable because companions have not joined, start or
stop the same supervised Player 1 AI session from SkyLoft through the proxy's
allowlisted action channel:

```sh
ssh root@100.124.74.13 'printf "ai_on\n" > /tmp/flxtr-som-ai-action'
ssh root@100.124.74.13 'printf "ai_off\n" > /tmp/flxtr-som-ai-action'
```

Do not create `/tmp/flxtr-som-ai-enabled` directly: `ai_on` also arms the
proxy's physical-controller takeover cleanup. This starts AI control of Player
1; it does not create or mutate a companion party member.

Mapping basis: the live work-map and collision-cache layout follows the
read-only reverse engineering in `cidwel/som-decomp` (`docs/maps.md` and
`docs/hero-movement.md`), while the 16×16 block/collision organization is
also documented by the Secret of Mana map-editor project. The runtime
cross-check remains mandatory because this target is the Turbo ROM hack.

**Do not feed serialized savestates** to `--wram-file`. Libretro
`retro_serialize()` outputs core-specific state, not plain WRAM.

## Known constraints

The public memory map documents enemies at `7EE600`, `7EE800`,
`7EEA00` and character structures at `7EE000` in 0x200-byte slots.
It does not establish a reliable enemy-slot-active flag, so the agent
is told `active: null`; it must not treat every slot as an active enemy.
Player position and map ID remain unknown until verified against the ROM/core.

Reference: https://wiki.superfamicom.org/seiken-densetsu-2

## P2/P3 recruitment experimentation

Girl and Sprite can potentially be injected through a **separate, opt-in
WRAM write interface** after their initialization and party/event flags
are mapped. This is not equivalent to selecting graphic 0x81/0x82 or
setting companion availability to 1. A working implementation needs
coherent character stats, slot activation, AI/control configuration,
sprite loading, and event state. Avoid writes against your only save.
Use disposable savestates, compare before/after naturally recruiting
each member, and only then introduce a reversible experimental command.

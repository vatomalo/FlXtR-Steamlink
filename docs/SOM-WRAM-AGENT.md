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

## Inn (taught by demonstration)

Show the AI the inn once by walking there yourself:

```sh
systemctl --user stop flxtr-som-ai.service
python3 tools/som_teach_inn.py
systemctl --user start flxtr-som-ai.service
```

The tool only reads WRAM. While you walk it stores every map crossing and the
walkable tiles around you in the AI's history database. Stop on the tile in
front of the innkeeper, press Enter, and say which side they are on. That tile
becomes the inn; Candy heals or other HP rises can no longer move it.

When survival mode reaches that tile, the bridge taps toward the innkeeper
(and aborts if the tap moved the hero), presses Confirm to talk (aborts if no
dialog starts), then presses Confirm at most ten times until HP is full and
the game is back in field mode. Without a full heal it presses Cancel until the
dialog closes and disables the inn for the rest of the run. Not enough GP or a
"No" default therefore fails safely. Reteach if the inn changes.

## Goals: hunt, heal, shop

Each decision the bridge picks one goal, in this order:

1. **HP below half:** use Candy if carried (and no enemy within 64 px);
   otherwise retreat along learned maps to the taught inn and stay there.
2. **Healthy and rich:** with fewer than four Candy and at least
   `SOM_AI_SHOP_GOLD` GP (default 500), walk to the taught Candy shop and buy
   one, repeating until four Candy or below the threshold. Enemies met on
   the way are still fought.
3. **Otherwise hunt:** head `SOM_AI_HUNT_DIRECTION` (default `west`) and
   fight what is met. Qwen can still pick engage/avoid, but not the heading;
   use `none` to let Qwen choose the heading again.

### Teach the Candy shop

```sh
systemctl --user stop flxtr-som-ai.service
python3 tools/som_teach_shop.py
systemctl --user start flxtr-som-ai.service
```

Type your current GP, walk to the shopkeeper, press Enter on the tile in
front of them, say which side they are on, then buy exactly one Candy by hand
and close every dialog. The proxy records your physical presses (it presses
nothing while teaching), and the GP you type before and after identifies the
gold byte. The AI replays that exact sequence and requires one more Candy and
less gold; otherwise it cancels out and stops shopping for the run. A shop
whose cursor remembers a different position could sell a different item; the
Candy check then reports failure. Proxy rebuild required for recording.

## Bridges and other tiles the AI will not take alone

The AI steps only on tiles verified as plainly open. Bridges switch the hero
between tile layers and read as conditional (`?`), and on some bridges the
tile-ID cross-check fails entirely, so the AI never crossed them. Teach once:

```sh
systemctl --user stop flxtr-som-ai.service
python3 tools/som_teach_path.py     # walk across the bridge, press Enter
systemctl --user start flxtr-som-ai.service
```

- Every tile you stood on becomes walkable for the AI (`walked_cells`),
  whatever later grid readings say, and while the tile check is unavailable
  it keeps following those tiles (`intent=follow_taught_path`).
- The collision attributes of each conditional tile you crossed are learned
  (`passable_signatures`). Tiles with identical attributes behave
  identically, so every bridge built from the same tile types becomes
  walkable on any map without teaching it again. `som_teach_inn.py` and
  `som_teach_shop.py` learn the same way along their walks.

If the AI chooses the same step eight decisions in a row without its pixel
position changing (for example under a bridge, where taught bridge tiles are
not walkable from the lower layer, or against an NPC), that step from that
tile is blocked for 200 decisions and the planner picks another way. The log
shows `intent=replan: no progress with step_... from tile [...]`.

Inns are recognised by look only as **candidates**: the 7x7 tile pattern around
a taught inn is stored, and a map whose surroundings match at least 80 percent
is logged and shown as "looks like an inn". The innkeeper is a sprite, not a
tile, and shops use similar counters, so the AI does not talk to a candidate
on its own (Confirm in a shop can buy things). Teach it with
`som_teach_inn.py` to use it. Tile IDs may be tileset-specific across maps;
this has not been validated on hardware.

## Experiment: spawn a monster copy (first game-memory write)

`clone_enemy` is the proxy's only write to game RAM. It copies one live
monster object (0x200 bytes) into an empty monster slot (7EE600/E800/EA00),
32 px to the right, after taking a full emulator snapshot; `undo_spawn`
restores that snapshot exactly (everything since is lost). Stand near a Rabite
so it is loaded, then on SkyLoft:

```sh
python3 tools/som_spawn.py clone
python3 tools/som_spawn.py undo
```

**Result on hardware: the copy is not drawn.** Drawing goes through a sorted
actor list and per-actor piece buffers at `$7E:F800` (som-decomp
`docs/graphics.md`), which a slot copy does not join. Use the map-record
method below for visible monsters.

Basis: `cidwel/som-decomp` `docs/rabite.md` (monster slots 3-5, object
fields, spawner `$C0:DE3B`). The game's own spawner is not called, so engine
state outside the slot (sprite allocation, map object records) is not updated:
the copy may not draw, act, or die cleanly. Running the real spawner for an
arbitrary monster id would need executing game code from the proxy, which is
not implemented. Only the explicit tool can send these commands; the model
cannot. Keep a normal backup save before experimenting.

## Movement rules and eagle-eye map scans

Tile rules follow the real movement code (som-decomp `docs/maps.md` 7 and
14.2, `docs/hero-movement.md` 7): a move is refused only into a type-3 tile,
a type-1 tile at height 2, or a type-2 tile at height 1. Ramps and stairs
(including the log-bridge ends), bridge decks (types 4-7), layer-switch tiles
and diagonal half tiles are walkable; the game slides the 13x9 px hero box
around corners at 2 px per frame, so a held direction enters one-tile gaps.
Only moving floors and special ground stay conditional.

Map memory stores height-independent kinds (`h1`, `h2`, `pass`, `open`,
`blocked`), and routes search tile and height together, so a bridge reached
up its ramp is used even though its raised ends look like walls from the bank.

Every decision acts on the 7x7 close-up. Because each WRAM snapshot already
contains the whole work map, wider views cost no extra SSH traffic: a 65x65
scan when a map is entered (`[ai] eagle eye: mapped N tiles`) and a 33x33
scan every 10 decisions feed the learned map (about 11 ms and 3 ms on
SkyLoft-class CPUs).

## Walked tiles keep their height

A tile the hero has stood on is proven walkable, but not at every height.
Walked tiles used to be stored as plain ground (height 0), so on the log
bridge of map 130 the route stepped from the deck (height 1) straight down
onto the height-2 bank, which the game refuses; from the next tile the route
pointed back, and the hero swung left/right for minutes
(`frontier 39,30 is 41/42 known tiles away`). Walked tiles now keep the kind
read from the map (`h1`, `h2`, `pass`, `open`); only tiles that looked
impassable are upgraded, to `pass` (keeps the arrival height).

## Map loading guard

The map id (`7E00DC`) changes one or two snapshots before the work map, the
map size and the hero position (live log, crossings 130 -> 129 and 130 -> 131).
Those snapshots passed the tile check with the old map's data, so the
eagle-eye scan on entry stored the previous map under the new id and learned
crossings recorded wrong tiles. After a map id change the tile context is now
`map_loading` (unverified) until a loader mode (transition/event) is seen, the
map size changes, or 8 field samples have passed. On first start with this
version the observation log is replayed once with the guard to re-learn the
crossings it contains.

## Multi-map routes from the ROM exit table

`tools/som_rom.py` reads the documented exit tables of som-decomp
`docs/maps.md` sections 2 and 8 from the ROM file over SSH (read-only, `cat`
and `sha1sum`; parsed tables are cached in
`~/.local/state/flxtr/som-rom-exits.json` by ROM digest):

- `$C8:7000` map headers (byte 3: trigger base, entry-event flag),
- `$C8:4000` event lists (one word per trigger tile id),
- `$C8:3000` transitions (target map, start tile, layer).

Turbo relocates some tables (map 182's header and the event lists of maps 51,
133, 183, 248 and 250 are out of order); those maps get no ROM exits. Only
plain transitions (event words `0x800-0xBFF`) are used; scripted exits (story
gates, dialogs) are learned by walking them.

When a map has loaded, its live work map is scanned for trigger tile ids and
each exit tile is stored with its target (`map_exits`; `[rom] map 129: 4 exit
tiles to maps [15, 128, 130]`). Every real crossing is cross-checked: the
trigger tiles within two tiles of the last position must predict the map
actually entered (`rom_exit_checks`). ROM exits are used only after at least
one confirmed and no contradicted crossing; one contradiction disables them
and routes fall back to learned crossings. On the live Turbo log: 8 confirmed,
0 contradicted (130 -> 129, 130 -> 131, 131 -> 130, 131 -> 259, 259 -> 131).

Travel to a place on another map (inn, shop, errands) searches the chain of
maps over learned crossings plus trusted ROM exits, so maps never walked
between can be chained (`via maps 131>130>129>128`), then walks onto the
nearest reachable exit tile of the first hop. A reachable ROM exit tile is
preferred to a learned crossing, whose recorded tile can be off for return
doors and scripted moves.

## Face the enemy before swinging

A swing only connects in the facing direction. The hero's facing is `E010`
(som-decomp `docs/hero-movement.md` 2: 0x00 up, 0x01 down, 0x02 right, 0x82
left; a diagonal faces left/right). It is trusted after three steps that
really moved the hero agreed with it and contradictions stay below 10 percent
(verified live on Turbo). The planner attacks only while facing the enemy's
dominant direction, otherwise it taps toward the enemy first (or along the
other axis when a wall is in the way). Until the byte is trusted it always
taps toward the enemy on the decision before a swing.

A turn tap toward an adjacent enemy (or grass) does not move the hero, so it
is never counted by stall detection; before, eight such taps blocked that
direction for 200 decisions and the hero swung facing the wrong way. An
enemy counts as in front when it is ahead along the facing and no farther
sideways than ahead (at least 20 px), so an enemy near the diagonal does not
cause a new turn before every swing.

### Two enemies on opposite sides

When the two nearest enemies within 48 px are on roughly opposite sides
(cosine of their directions below -0.3), the hero takes the open step that
puts both on one side (raising that cosine by at least 0.5, keeping 12 px to
each), then fights them facing one way. Not at critical HP, and not when no
open step helps (a corridor): then it fights the nearest.

## Reward: GP gained, HP lost

Every settled field sample is scored: `+1` per GP gained and `-2` per percent
of max HP lost (`SOM_AI_GP_REWARD`, `SOM_AI_HP_PENALTY`). HP gains and GP
spending score nothing; a gold jump above 5000 is a misread and is ignored,
and GP is only rewarded with a calibrated gold address
(`som_teach_shop.py`). Points are booked per map and per nearest on-screen
enemy type in `reward_ledger` and logged as `[reward] +20.0 (+20 GP, -0 HP,
map 129, enemy type 0x0a)`. An enemy type with at least five scored events
and a net score of -200 or worse (two full HP bars more than it paid) is no
longer engaged; the hero backs away when one comes within 64 px
(`intent=evade_enemy`). Per-map scores go into the Qwen goal summary.

## Weapon levels and status effects

Decoded per hero from som-decomp `docs/wram-companion-map.md` and
`docs/hit-reactions.md`, with Turbo cross-checks:

- Weapon: type `+0x1E4`, row `+0x1E8` (row = type x 9 + tier), levels
  `+0x1C0` (eight nibbles, even type = high nibble; unverified in som-decomp),
  charge stage `+0x19B`. Trusted only when row / 9 equals the type, the weapon
  animation type `+0x068` equals it, every level is 0-8, the charge stage is
  not above the weapon's level, and for the boy the owned-weapon byte
  `$CC54 + type` is owned, equipped (bit 6) and has the same tier. Live
  Turbo: `[weapon] sword tier 0 level 1`.
- Status word `+0x190`: slow 0x0004, frozen 0x0020, petrified 0x0040, balloon
  0x0100, pygmy 0x0200, poison 0x2000, engulfed 0x4000, dead 0x8000; other bits
  are reported as unknown. Trusted unless an active hero has the dead bit with
  HP left. While frozen, petrified, balloon or engulfed (the game zeroes the
  pad, mask 0xC160) the planner waits instead of mistaking refused steps for
  walls (`controls locked by status frozen`).

Both are logged when they change and go into the Qwen goal summary when
verified.

## Charged attacks

The proxy action `charge` holds the attack button (B) for up to 900 frames;
any later command (`wait`) releases it, which performs the charged attack
(som-decomp `docs/weapons-melee.md` 3: the charge counter `E01A` fills after
the swing and the weapon gauge, then the stage `+0x19B` rises every 90 frames
up to the weapon level; damage x(2 stage + 4)/4). No game memory is written.

The planner charges instead of swinging when it faces an enemy with at least
40 HP, the weapon reading is verified with level >= 1, the hero is above half
HP, and charging has not failed this run; the stage is the weapon level,
capped at 2 (1.5 s per stage). `tools/som_combat.py` runs it closed-loop:
the counter must start within 3 s, each stage must come within 2.5 s, a drop
in hero HP (a hit cuts the charge) releases at once, and after the release
the weapon gauge must start recharging. The enemy's HP loss is logged
(`[charge] stage 1 attack released; enemy damage 60`). Two failures disable
charging for the run. Needs the rebuilt `som_ai_proxy_libretro.so`; an older
proxy ignores `charge`, which fails closed after two attempts.

Kill GP arrives after the monster has left its slot, so GP gained up to 30
samples after an enemy was near is credited to that enemy type.

## Qwen chooses goals

In the hybrid policy Qwen now picks a goal, not buttons or tiles. The bridge
builds a short summary from memory (map, HP, level, gold, Candy, enemies near,
inn and shop maps, known maps, reward per map) and an allowlist of goals the
planner can carry out right now:

- `hunt_here` (default), `engage_enemy` / `avoid_enemy` (enemies near),
  `explore_<heading>` (only with `SOM_AI_HUNT_DIRECTION=none`),
- `go_to_inn` (inn taught, HP below 90 percent, route known),
- `go_to_shop` (shop taught, Candy affordable and fewer than four),
- `return_to_potos`, `hunt_map_N` (the two best-scoring reachable maps).

Only an offered goal is accepted; anything else keeps the current goal.
Travel goals stay chosen until arrival or 600 decisions so a small model
cannot undo a journey, and use the multi-map routes above. Survival below
half HP, fighting enemies met on the way and the closed-loop inn/shop macros
still override any goal. Log: `[qwen-director] goal=hunt_map_128 (offered
...)`, `[goal] hunt_map_128 reached; hunting here`.

## Cutting grass

Grass blocks the way until it is hit; then the game replaces it with an open
tile. The AI learns this by watching: when a blocking tile right next to the
hero turns into a different, open tile, the old tile's four attribute bytes
are stored as cuttable (`cuttable_signatures`). Any tile with the same bytes,
on any map, is then treated as grass: routes may pass through it, and the
planner taps toward it (to face it) and attacks (`intent=cut_grass`). After
three attacks without a cut, that tile is avoided for 200 decisions.

Teach it once by cutting a patch yourself:

```sh
systemctl --user stop flxtr-som-ai.service
python3 tools/som_teach_path.py   # face some grass, attack it, press Enter
systemctl --user start flxtr-som-ai.service
```

The AI also learns whenever one of its own attacks cuts grass.

Grass grows back when the map reloads, so a tile the hero once walked
through (after cutting it) is cut again, not walked into, while the live grid
shows known grass there. When a step is refused without the hero moving
(for example from a diagonal ledge tile) and known grass lies one or two
tiles that way, the AI swings at it instead of blocking the step; a boxed-in
hero also looks two tiles out for grass. Pots or other
breakable tiles that behave the same way are learned the same way.

## Spawn a real Rabite (native map spawn)

The game creates monsters from each map's 8-byte object records while the map
loads (som-decomp `docs/maps.md` section 10: pointers at `$C8:7000`,
record `+5` = object id, `0x00` = Rabite). A monster created this way is
complete: drawn, animated, AI, death, EXP and drops. `som_ai_proxy` can
change a record's object id in its in-memory ROM copy at game load; the ROM
file and saves are never modified, and every patch line names the byte it
expects so a different ROM build is left alone.

```sh
python3 tools/som_map_records.py list        # records of the current map
python3 tools/som_map_records.py rabite 2    # turn monster record 2 into a Rabite
# quit and relaunch Turbo, enter the map
python3 tools/som_map_records.py clear       # undo; relaunch again
```

```sh
python3 tools/som_map_records.py spawn 0 0x0a --map 130   # record 0 -> monster 0x0a
```

Turbo check (2026-11-13): the `$C8:7000` table is used by Turbo, but a few
maps are relocated (map 181/182 record bounds are out of order and are
refused), and Turbo also spawns monsters that are in no record (identity 0x0a
on map 129, monsters on map 128 which has no records). A map is patched only
when a monster the game really spawned there (live WRAM or the AI's
observation log) has one of its record ids; on map 130 the records say Rabite
and only Rabites were seen. `spawn` accepts ordinary monster ids 0x00-0x56.

Only monster records are replaced by default (`--allow-npc` overrides; that
can break events). The record's own flag range still decides whether it
spawns. The tool refuses to continue if the `$C8:7000` table does not look like
Secret of Mana's, because the Turbo hack may have moved it. Patches live in
`/home/apps/retroarch/cores/experimental/som_ai_rom_patch.txt`.

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

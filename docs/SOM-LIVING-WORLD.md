# Secret of Mana Turbo: Ultima VII-style living world

Branch `feature/som-ai-memory-agent-20261112`. This is the first slice: engine
findings, a persistent world clock, data-driven NPC schedules, an off-screen
simulation, read-only reconciliation with the live game, and an **opt-in,
not yet deployed** native NPC walk in the proxy. Read with
`docs/SOM-AI-HANDOFF.md` and `docs/SOM-WRAM-AGENT.md`.

Tags: **[V-Turbo]** verified on the Turbo ROM or on live Turbo WRAM in this
work; **[V-decomp]** verified by `cidwel/som-decomp` on the US ROM (executed
or byte-exact sources), not re-checked on Turbo; **[H]** hypothesis.

## 1. Status in one table

| piece | state |
|---|---|
| NPC representation, slots, binding to map records | verified on live Turbo (map 131, two snapshots) |
| native NPC movement primitive (ACTOR_WALK) and the routines it uses | verified byte-exact on the Turbo ROM, including two Turbo relocations |
| world clock (persistent, deterministic, catch-up) | implemented and tested |
| schedule format and Potos data for one NPC | implemented and tested; locations verified from ROM + live terrain |
| off-screen simulation, story overrides, interruptions, encounter memory | implemented and tested (SQLite) |
| daily Qwen director (one validated schedule change per NPC and day) | implemented and tested; smoke-tested against local Ollama (section 6.1) |
| reconciliation + route planning + native walk commands | implemented; runs read-only against live WRAM (re-checked 2026-11-13: map 131, slot 8 bound, holds during events) |
| proxy `npc_walk` (game-RAM write) | implemented, unit-tested, ROM-gated, off by default; **deployed 2026-11-13 with the enable marker** |
| visible NPC movement in the running game | **demonstrated live 2026-11-13** (section 10.1) |
| NPC disappearing indoors / crossing maps natively | not possible without a further mechanism (section 9) |

## 2. Reverse-engineering findings

### 2.1 Where NPCs come from [V-decomp, V-Turbo]

- Every map has 8-byte **object records** after its header (`$C8:7000 +
  2*map` -> bank `$C8`). Bytes: `+0` event-flag index, `+1` min<<4|max of that
  flag for the object to exist, `+2` x tile (bit 7: keep when off-screen),
  `+3` y tile (**bit 7: static, native AI off**), `+4` facing/layer, `+5`
  kind (`>= 0x80` = NPC), `+6/+7` talk/contact event word.
- At map load the records are copied to the WRAM cache `$7E:C800 + 16*n`
  (state at `+0`, record bytes at `+8..+15`) and spawned into **actor slots
  3..11** (`$7E:E600 .. $7E:F600`, 0x200 bytes each) by the same spawner as
  monsters (`$C0:DE3B`). Heroes are slots 0..2 and are never NPCs.
- Turbo live, map 131 (Potos): the ROM records decode exactly as in
  som-decomp (10 records); with story flag `0x10` = 3 the seven eligible
  NPCs occupy slots 3..9. `obj+0x34` holds the cache offset (`record * 16`),
  which binds a live object to its record: slot 8 -> `0x0080` -> record 8.
- Spawn pixel on Turbo: `(16*tx + 8, 16*ty + 4)` (all five static NPCs of
  map 131; som-decomp states `+4` for x on the US ROM) [V-Turbo].
- Object fields used here: `+0x00` active (1), `+0x02/+0x04` pixel X/Y,
  `+0x06/+0x07` sign-magnitude velocity, `+0x0A` walk frame counter,
  `+0x0B` layer (bit 7) and floor height (bits 0-1), `+0x10` facing (0 up,
  1 down, 2 right, 0x82 left), `+0x11` pose, `+0x1C`/`+0x60` state, `+0x3D`
  kind, `+0x3E` event word, `+0x42` busy, `+0x44` bit 7 static / bit 6 keep
  off-screen, `+0x71` presentation flags.

### 2.2 Native behaviour [V-decomp; Turbo gate V-Turbo]

- Non-static NPCs run the shared **wander script** (AI entry `0x54CD`):
  random facing, up to six 10-frame steps; a step that does not move ends the
  walk. Static NPCs never run AI: the per-object step skips it when
  `obj+0x44` bit 7 is set.
- **Turbo moved that gate** to its expanded bank: `$C0:F8DF JSL $F0:78F9`,
  whose first instructions are `LDA $E044,X / BPL / RTL` (skip AI when
  static). Turbo also adds an off-screen check there that zeroes `obj+0x06`
  for non-static actors outside the screen. Live confirmation: across two
  snapshots a minute apart the five static NPCs of Potos kept their exact
  spawn pixels while the two wanderers (0xCA, 0xC5) moved.
- Talking: the talk scan `$C0:B851` checks facing/distance/layer, stops the
  NPC, turns it to the hero and starts its event; `$7E:003A` holds the
  talked-to object while `$D0` (event VM state) is non-zero. A talk works on
  static NPCs and on walking ones.
- Off-screen removal (`$C0:FC2E`) is disabled by record `+2` bit 7; every
  Potos record has it, so Potos NPCs stay loaded while the map is.
- Leaving a map discards its objects. On a **return door** (house interior
  back to the village) the positions saved at `$7E:CD70` are restored, so a
  wandering NPC is where it was; on any other entry it respawns at its
  record. NPCs never leave their map natively.
- Story control: records appear/disappear by flag ranges (`$7E:CF00 + f`;
  Potos uses flag `0x10`), re-tested on load and by event command 0x09.

### 2.3 Native movement primitive [V-decomp; V-Turbo byte-exact]

Cutscenes move NPCs with event command **ACTOR_WALK** (0x32, `$C1:EEEA`).
For an NPC it writes exactly ten fields: `+0x1C=0, +0x13=0, +0x0A=frames,
+0x67=0, +0x42=1`, velocity `+0x06/+0x07` (0x02/0x82 = ±2 px/frame), facing
`+0x10`, `+0x30=0xFF`, pose `+0x11` (1, or 0 and facing 0 when `+0x71`
bits 0x60 say so). Then, every frame:

- the **NPC mover** `$C0:D229` applies velocity through the terrain queries
  (`$C1:B93E..`), so blocked steps do not move: collision is native;
- the **countdown** decrements `+0x0A` and at 0 clears velocity, busy and
  pose. On Turbo this code was relocated: `$C0:FBFE JMP $EB86`, and
  `$C0:EB86..EBB5` is the same logic (disassembled; identical effect).

Checked by assembling the som-decomp sources of `$C1:EEEA` and `$C0:D229`
and comparing with the Turbo ROM (byte-identical), and by disassembling the
two Turbo relocations. Fingerprints of all six ranges are in
`tools/som_npc_native.py` (SHA-1) and `src/som_npc_walk.h` (FNV-1a); the
proxy refuses NPC walks when any differs. Not verified: the exact NPC speed
(som-decomp measured 2 px/frame dropping to 1 px/frame), so walks are
closed-loop.

### 2.4 Doors and transitions [V-decomp, V-Turbo via existing tools]

Doors are trigger tiles (`id >= 0xC0 - (header byte 3 & 3)`), each mapped
through the event list to a transition (`$C8:3000`: target map, start tile,
layer). `tools/som_rom.py` already reads these and the AI history confirms
them; e.g. door `0xC4` at Potos (27,24) -> map 257 start (12,36). Only the
controlled hero triggers them (`$C0:B63A`), so NPCs cannot use doors.

## 3. Architecture evaluation

| | native ROM | external | **hybrid (chosen)** |
|---|---|---|---|
| schedules/time | new 65816 code + SRAM layout | Python | Python |
| movement | native | position writes (unsafe) | native ACTOR_WALK effect via verified fields |
| collision | native | must be re-implemented | native mover + planned routes |
| off-screen | impossible (objects unloaded) | cheap | cheap, SQLite |
| persistence | needs save format changes | SQLite | SQLite, saves untouched |
| story safety | high risk | medium | static NPCs only, events always win |
| Steam Link cost | none | WRAM export only | WRAM export + one tiny write per walk |
| reversibility | ROM patch | stop process | delete marker / stop process; map reload restores |

A pure ROM implementation cannot simulate unloaded maps and would change the
save format. A pure external one would have to write positions, which
bypasses the terrain/priority fields the engine maintains. The hybrid keeps
time, schedules, memory and planning on SkyLoft and lets the original engine
do the walking and collision on the Steam Link.

## 4. World clock (`tools/som_world_clock.py`)

`minutes = base + (real - anchor) * rate` while running; frozen while
paused. Real time is passed in, so results are deterministic and independent
of SSH/LLM latency. Default rate 0.2 game minutes per real second (one game
hour per five real minutes); a new world starts at 06:00 on day 0. Phases:
night < 05:00, dawn < 07:00, day < 18:00, dusk < 20:00, night. Seven
weekdays. State in table `world_clock`; on reopening, the offline gap is
credited up to `max_catch_up_minutes` (default one game day). `set_time`
only moves forward. CLI: `som_living_world.py clock pause|resume|set
HH:MM|rate N`.

## 5. Schedule format (`tools/som_schedules/potos.json`)

JSON (no YAML dependency on SkyLoft). `locations` carry map, tile, layer
and a mandatory `evidence` string; `abstract` locations (house interiors on
another map) need a `proxy` on-map place. Each NPC has a persistent `id`,
`home`, `workplace`, a `native` binding (`map`, `record`, `kind`, `static`),
optional `story` flag ranges, `personality`, per-activity `lines`, and a
`schedule` of `{time, activity, destination}` with destinations being a
location id or `home` / `workplace` / `stay`. Activities: `wake_up eat travel
work socialize wander patrol return_home sleep idle shop_open shop_closed`.
Loading rejects unknown activities/locations, duplicate times, missing
evidence and incomplete bindings.

## 6. NPC state machine and off-screen simulation (`tools/som_living_world.py`)

State per NPC (table `npc_state`, JSON): `location`, `destination`,
`activity`, `travel {from, depart, arrive}`, `interrupted_until`,
`override`, `last_sim`. `advance(now)` walks only the schedule boundaries
between `last_sim` and `now` (O(boundaries); nothing per frame): finish a
travel that has arrived, start the next entry, and open a travel window when
the destination differs (`travel_minutes`: Manhattan tiles x 0.05 + 2 per
map change, at least 1). Interruptions (talking) hold the NPC for 5 game
minutes and push a pending departure. Story overrides: when a `story` range
excludes the NPC (Potos record 8 exists only for flag 0x10 in 0..6), the
state becomes `story_absent` and it is never driven. A validated director
hook (`override_day`) can change one existing entry's destination for one
day, limited to places the NPC already uses; this is the only path for
Qwen, which therefore never touches memory or controls frames.

Encounter memory (table `npc_encounters`) records talks; context lines use
it (repeat visits), the clock (night warnings) and the activity (sleeping).
Tables live in their own database `~/.local/state/flxtr/som-living-world.sqlite`,
separate from the AI history.

### 6.1 Daily director (`tools/som_npc_director.py`)

Opt-in with `run --director [--ollama-url URL --model M]`. At the first tick
of each game day, for each NPC, the director builds a menu of at most six
one-slot changes (option ids `A`..`F`) plus `K` (keep the routine):

- every option is an `override_day` the simulation already validates (an
  existing entry time, a destination the NPC already uses or its home);
- `sleep` and `wake_up` slots are never offered, nor slots that have already
  started;
- places are interleaved by personality appeal (diligent -> workplace,
  sociable -> social places, unsociable -> home), evening slots first, so
  the menu reads like "work late", "stay out", "stay in tonight".

The prompt contains the name, personality, how often the player talked to
the NPC yesterday (encounter memory) and the menu; no addresses or
coordinates. Qwen's reply must contain exactly one offered id, otherwise it is
`K`. The call runs in a worker thread, so `tick` never blocks the loop; an
answer that arrives after its slot has started is dropped (`stale`). Errors
and timeouts are `K`. Every day's decision is stored in `npc_director_days`
(`choice`, `source` = qwen / error / stale / rejected / no_options), so each
NPC is asked at most once per game day, even across restarts.

Smoke test against the local Ollama (2026-11-13, throwaway DB): `qwen3:0.6b`
answered `"Answer: D: at 18..."` in 8.7 s -> override "18:00 socialize at
lane west" stored; `qwen3.5:4b` hit the 45 s timeout on a cold load -> `K`
(`error`), schedule unchanged.

## 7. On-screen reconciliation and pathfinding

When the player is on the NPC's map, `NativeDriver` binds the identity to
the live object (kind + record), takes the desired place (`on_map`, with
abstract homes mapped to their proxy), and plans a 4-way BFS route over the
live work map on the NPC's own layer and floor height (same tile rule as
the hero planner; trigger tiles such as doors are never entered; tiles of
other NPCs and heroes are avoided). It sends only the straight start of the
route (at most 4 tiles, 8 frames per tile so it undershoots), then observes:
no movement means that step is blocked (re-plan without it); three blocks
from one tile pause the NPC for 40 cycles. It waits during events, menus
and transitions, never drives wandering NPCs, and yields when its next tile
is next to a hero (no permanent blocking). Because ACTOR_WALK is the
engine's own walk, the NPC animates and collides natively: no teleporting.

## 8. Dialogue

Native dialogue is untouched: the event of the record still runs. While the
player talks to a scheduled NPC (`$D0 != 0` and `$7E:003A` = its object),
the runner records the encounter, interrupts the schedule and shows one
context line through the proxy's existing overlay (`/tmp/flxtr-som-ai-message`,
drawn on a copy of the frame). Native text replacement would need event
scripts (`$C9/$CA` banks, inline text) to be rewritten, which this slice
deliberately does not do. `tools/som_dialogue.py` (another work in
progress in this worktree) reads native text and is complementary.

## 9. Engine limitations

- NPCs exist only on their own map, from its records. "Going home" into a
  house on another map cannot be shown natively; the prototype walks the NPC
  to its doorstep (the house door is the ROM exit at (27,24)) and the
  simulation tracks it inside. Making it vanish needs either a flag range
  keyed to an unused event flag (writes `$CF00`, which is saved in SRAM:
  save-affecting) or verified hiding of an NPC object; both need approval.
- A normal map entry respawns the NPC at its record, so after re-entering
  Potos it walks from its spawn to its scheduled place (visible, natural);
  a return door restores the last native position.
- Only static NPCs are driven. Wandering NPCs would need their script
  stopped (record bit 7 via a ROM record patch, the existing load-time patch
  mechanism) before they could be scheduled.
- NPC speed is not exactly known; planning is closed-loop.

## 10. Prototype

NPC: Potos record **131#8**, kind `0xC7`, static, event `0x109`, flag
`0x10` range 0..6 (an ordinary villager beside a house door; Elliott and
Timothy (`0xC0/0xC1`) and the flag-0x0D villager are left alone).

| time | activity | place (map 131 tile) |
|---|---|---|
| 06:00, 07:00 | wake up, breakfast | home (house 257 (12,36); on-map: doorstep (27,25)) |
| 08:00, 09:00, 13:00 | travel, work | lane west (24,30) |
| 12:00, 18:00 | lunch, socialize | lane east (33,28) |
| 21:00, 22:00 | return home, sleep | doorstep (27,25), facing the door |

Read-only run against live Potos WRAM (the snapshot was copied and its event
byte cleared to plan as in field mode): at 09:00 the driver planned
`npc_walk 131 8 199 472 404 1 32` (down 4 tiles toward the lane), at 21:30
`... 3 16` (left 2 tiles to the doorstep). Nothing was sent.

```sh
python3 tools/som_living_world.py status
python3 tools/som_living_world.py observe            # live WRAM, read-only
python3 tools/som_living_world.py run                # read-only + talk overlay
python3 tools/som_living_world.py run --live         # after approval only
```

### 10.1 First live run (2026-11-13, approved by the user)

Save and old proxy backed up to `~/backups/som-saves/20261113-214008/`
(SkyLoft); new proxy (md5 `04bd131b...`) deployed with the game closed;
enable marker created; world clock set to 09:00; player in Potos.
`run --live` sent nine `npc_walk` commands, all accepted by the proxy:

| command (dir, frames) | live pixel before -> after |
|---|---|
| down 32 | (472,404) -> (472,415), first walk while an event was ending |
| down 32 / down 16 / down 8 | -> (472,449) -> (472,468) -> (472,481) |
| left 32 / 24 / 8 / 8 | (472,481) -> (438,481) -> (413,481) -> (400,481) -> tile (24,30) |

The villager walked from its spawn (29,25) to the west lane (24,30), down
then left, with its native walk animation, and stopped there ("at place").
The other six Potos NPCs kept their slots and positions; the game stayed in
field mode. Measured speed: about 1 px/frame (34 px in 32 frames, 25 px in
24), consistent with som-decomp; walks stay closed-loop. Each runner cycle
takes about 2.5 s (WRAM fetch over SSH), so a 5-tile walk takes ~20 s real.

**Feature toggle / restoring original behaviour.** The proxy accepts NPC
walks only while `/home/apps/retroarch/cores/experimental/som_ai_living_world_enable`
exists (absent by default) and only when the ROM fingerprints match. Remove
the marker or stop the runner and the game is unchanged; NPC positions return
to their records on the next map load. Saves, ROM file and story flags are
never written.

## 11. Tests

```sh
PYTHONPATH=tools python3 -m unittest discover -s tools -p 'test_som_*.py'
make test-som-ai-proxy test-som-ai-player2
```

New: `test_som_world_clock.py`, `test_som_npc_schedule.py`,
`test_som_npc_native.py`, `test_som_living_world.py`,
`test_som_npc_director.py` (menu validity, no past/night slots, once per
day, invalid/failed replies keep the schedule, stale answers dropped,
non-blocking, simulation follows a chosen override); the proxy test checks
command parsing, every refusal gate, the exact ten-field write set (nothing
else in WRAM changes), all four directions, presentation flags, the enable
marker, the ROM gate and the rate limit. The fingerprints were also checked
against the real Turbo ROM (accepted) and a one-byte change (refused).

## 12. Next steps (need your decision)

1. ~~First live NPC write~~ done (section 10.1). Still to check live: the
   evening walk back to the doorstep, talking to the NPC mid-walk, and
   leaving/re-entering Potos (expected: respawn at the record, then walk).
2. Make the NPC vanish at night (flag-range or hiding mechanism, section 9).
3. Wandering NPCs and shopkeepers via record bit 7 patches; merchant
   open/closed lines; guards with patrol routes on maps with monsters.
4. ~~Qwen director~~ done (section 6.1). Next for it: more NPCs, so that
   NPC-to-NPC choices ("meet X at the lane") become possible.

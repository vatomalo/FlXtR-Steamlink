# SoM enhanced combat: disciplines, controls, and evade discovery

Development branch: `feature/som-disciplines-controls-20261114`, based on the **working** familiar branch `feature/som-familiars-working-20261114` (commit `595d57e`). Do not merge older `clone_familiar_visual` experiments over `som_familiar.h`.

## Direct weapon switching and X/Triangle Fire Ball (14 November 2026, evening)

### Why weapons garbled, and the fix

The native `weapon_trace` (sword -> boomerang, captured 16:46 on the Steam
Link) plus the `cidwel/som-decomp` disassembly located the missing piece.
The game's equip refresh (`$C0:EA19` -> `$C0:EA41`) does two things:

1. Points the hero at the weapon's tables: `+0x2A` frame table
   (`D0:FFD0[g]`), `+0x65` script table (`+0x77 + 240*type`), `+0x6A`
   (`D4:C002[4g]`), `+0x7E` (`0x1040 + g<<8`), `+0x68` type, `7E:D030`
   graphics set (weapon row byte 3), `+0x69` palette id only if unset.
2. **Expands the weapon's 64 effect tiles from ROM bank `$D4` into the
   hero's WRAM buffer `7E:2000` (boy; 0x800 bytes).** The game streams that
   buffer into OBJ tiles `0x144+` itself. The old direct switch skipped
   this, so new weapon frames were drawn with the old weapon's tiles.

Turbo re-encodes bank `$D4` (per tile: 8 `(a,b)` row pairs, then 8 `c`
bytes) and patches the converter to match: planes `a/b/c & ~(b&c)`,
plane 3 = 0. The proxy's expansion of the Turbo ROM reproduces the live
sword buffer **byte for byte** (2048/2048). The VRAM tiles that changed at
native equip time (`0x491-0x498`, `0x582-0x589`) are only the HUD weapon
name; the native refresh itself is deferred (the trace shows `7E:D7F1=0x80`
and an unchanged `7E:2000` 12 frames later).

Implementation (`src/som_ai_proxy.c`, `som_equip_weapon_direct`):

- Fields and tiles are computed from the loaded ROM (`patched_rom`), with
  stats from the 12-byte weapon row `D0:1000+12*row`: attack/hit by row
  delta, crit `3*level + row crit` (halved for types > 4), level nibble
  `+0x1C0`, `+0x194`, `+0x199/19A`, `+0x1F7`. `+0x196` is crit base, not
  weapon progress (progress lives in `+0x1D0..1D7` and is untouched).
- **Fail closed:** no direct switch until, once per session, the model
  reproduces the game's own `7E:2000` and table pointers for the held
  weapon. Refused (use the ring): Turbo alternate-bank rows (byte 3 >= 0xEE)
  and rows with stat bonuses (byte 1 / byte 2 bits 0-1, upgraded tiers only),
  which need the full `$C0:4530` rebuild.
- Switches only while the hero is idle (`+0x1C`, `+0x60`, `+0x19B` zero,
  `$F1` bit 7 clear). B/Y held during a swing wait and switch when free;
  the game then sees a fresh press. Y never attacks with the wrong weapon;
  B falls back to the held weapon if a switch is refused. No VRAM writes.
- Not reproduced: the HUD weapon-name text and `7E:D7F1` (meaning unknown).

### X/Triangle: Fire Ball (experimental)

With a discipline selected, X in field play casts spell `0x0C` (Fire Ball);
L2+X still reaches the game. This reproduces what the ring does on confirm
(decomp `docs/summon-cast.md` section 2, same as its validated `Sim.cast`):
request `+0x170` spell, `+0x177` element 2, `+0x176` 1, `+0x178` nearest
live on-screen monster (slots 3-5) + 1, `+0x179/17A` 0, hit list
`+0x172..174` = FF, level `+0x171` from `+0x1C5` high nibble (capped at 7);
then `$C1:88C7` (unchanged in Turbo): `+0x111`, `+0x108`, `+0x4F`=CE,
`+0x1C`=40, `+0x30`=FF, `+0x11`=94, `+0x06..08`=0, `+0x60`=20, and the spark
tiles `D4:BA00` into `7E:2800`. The game's scripts do the rest, take MP on
the first hit and restore the weapon art when the hero is released.

- MP must cover the cost (spell row `D0:2AD0+64*id` byte 15 = 2). **The boy
  had 0/0 MP in the 16:46 trace, so X currently reports "not enough MP".**
- Not reproduced: the `Fire Ball Lv.n` banner; the spark palette for frames
  0-59 (Turbo filters palettes through `$F0:4A46`; the game copies the cast
  palette itself at frame 60).
- **Testing without MP:** turn the AI on (hold R2/R3 1 s), press a button
  for Gambits, move to `Infinite MP (test)` and press B (or Left/Right), then
  `Exit gambits: you play`. While ON the proxy sets the boy's current MP
  (`+0x186`) to 99 every field frame; the game still pays each cast. Turning
  it off clamps MP to the real maximum. It is not saved in the gambits file
  and resets on every game load. Avoid saving the game while it is ON.
- `x_spell` holds the spell id, ready for a future magic-selection menu.
  Only hostile single-target spells (row byte 8 = 0x81) are accepted.

Both need hardware playtesting. Nothing here writes VRAM or save data.

## Native weapon graphics trace (14 November 2026)

Keep the existing B/Y class weapon mapping and direct-switch design. The native
game can change weapons during field play, but the proxy has only copied the
hero's equipment/attack metadata; it has **not** captured the game's graphics
upload. The previous monster-style sprite cache was removed from runtime after
real Steam Link sprites appeared garbled. Do not restore it.

A new **read-only** `weapon_trace` action records exactly what the native ring
does to WRAM and VRAM, without modifying either. Use the newly rebuilt and
deployed experimental proxy, start **field play with a weapon already equipped**,
then on Steam Link (via SSH):

```sh
printf 'weapon_trace\\n' > /tmp/flxtr-som-ai-action
```

Open the **native weapon ring** with L2+Y, select a **different** weapon and
return to field play. Allow 12 frames to elapse. The proxy should log
`WEAPON TRACE native ...` and create:

- `/tmp/flxtr-weapon-trace.txt`: original-to-new weapon IDs and changed WRAM
  offsets / VRAM tile IDs
- `/tmp/flxtr-weapon-ram-before.bin` and `-ram-after.bin` (128 KiB each)
- `/tmp/flxtr-weapon-vram-before.bin` and `-vram-after.bin` (64 KiB each)

Transfer the four binary snapshots and report back for comparison. Run traces
on the same map for sword -> axe, axe -> sword, sword -> boomerang, and (if
possible) boomerang -> sword. Save backups first; no state is written by
the trace. If the native core reports no VRAM exposure, only the WRAM side is
available and no speculative graphics patch should be deployed.

After observing the original loader's tiles and WRAM staging flags, implement
the missing graphics operation as a **targeted addition to direct switching**.
Do not replace class weapon switching with artificial ring menu input.

## Current implementation

### Manual Class Progress and stall recovery (14 November)

- Open the Disciplines menu during **manual gameplay** with L2+R2 together (hold L2 and tap R2 also works). Up/Down selects a discipline; B selects its appearance specialization; A closes. While open, physical P1 inputs are swallowed by the proxy so the native ring menu does not open. No AI activation required.
- With AI running, use a button to open Gambits, then L/R to switch to Disciplines.
- The SkyLoft bridge no longer turns missing `inn_ready` / `save_ready` into an unconditional infinite `wait`. The strategic phase stays `inn` or `save`; the deterministic planner may still explore/defend while the taught interaction is unavailable. Once available, it resumes the committed trip. This avoids immobilizing the hero for thousands of decisions.
- These changes require **rebuilding/deploying the proxy** for the menu and **restarting the SkyLoft AI service** for bridge changes. Switching Git branches alone does not replace the installed Steam Link libretro core or a running Python process.
- Test with `make test-som-ai-proxy test-som-ai-player2`, plus Python tests. Do not deploy until the tests pass.


### Appearance profiles

- `src/som_class_palette.h`: pure, reusable 16-color SNES BGR555 transformations with palette index 0 left unchanged. Profiles: Swordmaster frost-blue, Monk crimson/gold, Ninja midnight-blue, Summoner grayscale white, and celestial familiar blue-white.
- Familiar calls `som_familiar_tint` using the celestial profile and **retains its existing friendly allegiance, map following, and anti-friendly-fire**.
- `som_disciplines.h` maps selection to a visual profile and draws a four-shade swatch in the Disciplines menu. **This is only an overlay preview**: selecting it does not currently recolor the actual hero sprite, because we have not verified the hero's correct CGRAM shadow location, sprite palette slot, and native palette reload path on the user's exact Turbo ROM. Do not guess by writing monster palette addresses into hero memory.
- Test with `cc -std=c11 -Wall -Wextra -Werror -Isrc tests/som_class_palette_test.c -o /tmp/som_class_palette_test && /tmp/som_class_palette_test`. Also run existing `tests/som_disciplines_test.c` and proxy tests.
- A next milestone is to locate the player character's actual palette and redraw path through read-only observation, persist the selected visual profile per character, and apply only after a safe save/restore strategy.


- `src/som_disciplines.h`: nine independent, classless discipline XP tracks and a specialisation marker. Selection has no gameplay effects, no level restrictions, no palette changes, and no persistence yet.
- Disciplines overlay renders next to the Gambits overlay when the AI is controlling the leader. L/R toggles the tab; Up/Down moves; B selects/deselects a specialization; A closes. All control interception is inside the **AI overlay mode**, so the normal game's ring controls are deliberately untouched.
- `som_disc_award` rejects unverified and duplicate event IDs. It is *not yet hooked to real combat outcomes*, so no XP is currently earned from playing. XP is not yet stored to disk.
- Existing friendly familiar, its targeting/anti-friendly-fire, tint, and cross-map restore are unchanged.

## Target combat control layout (NOT implemented yet)

| Gamepad | Proposed action | Status |
|---|---|---|
| B / Cross | Basic attack / held charge | Existing native action |
| X / Square | Secondary attack / weapon-flow strike | Proposed; currently ring/magic-related |
| Y / Triangle | Equipped active technique (e.g. Shunshin) | Proposed; currently native item ring |
| A / Circle | Evade / cartwheel | Proposed; currently native cancel/dash modifier |
| R1 | Block / guard | Existing Turbo patch |
| L1 | Weapon flow modifier / quick swap | Proposed; may conflict with Turbo QuickSpell |
| L2 + Y | Item ring access | Proposed remap, must remain accessible |
| L2 + X | Magic ring access | Proposed remap, must remain accessible |
| R2 or R3 long hold | Existing AI on/off toggle | Preserve |
| L/R in AI overlay | Gambits / Disciplines tabs | Implemented |

`src/som_ai_actions.h` currently maps Y to `open_ring`, X to `menu_x`, A to `cancel` and dash as A+direction; the live bridge's taught Candy/shop/inn/save macros depend on these logical actions. Do not change `som_action_button` blindly. Introduce a **physical -> logical virtual controller** layer, distinguish game/field/menu states and translate only while game state is verified. Maintain logical `open_ring` and `menu_x` for the bridge.

## Cartwheel / evade reverse-engineering

The `som_ai_actions.h` comment states no cartwheel ROM action is verified. Existing A+direction is interpreted as dash, **not** cartwheel. R1 block is verified in the Turbo patch. Published Turbo descriptions confirm R blocking, running alterations and QuickSpell control conflicts, but do not establish a cartwheel WRAM address.

1. Preserve the working binary and backed-up save.
2. Read `cidwel/som-decomp` movement/animation docs and identify candidate native movement states, frame counters, velocity, animation frames, and flags. Treat offsets from the original ROM as hypotheses.
3. Capture *read-only* WRAM before/during/after controlled physical moves (normal run, dash A+direction, block R1, any observed cartwheel) using existing exported WRAM snapshots. Record position deltas, animation ids, collision/height/priority state, recovery frames, and visual result.
4. Compare candidate fields across repeated trials to distinguish state machines from incidental timers. Confirm on the user's exact Turbo ROM and software stack.
5. If an actual cartwheel routine exists, invoke only through verified input or a guarded hook. If absent, implement an external evade with collision-checked movement and compatible animation, not a guessed RAM write.
6. Keep the existing gamepad layout intact until ring menus, taught automation macros, physical takeover and controller tests have all been updated and verified.

## Shunshin design (not implemented)

Use one validated action for AI and humans. First dry-run intended destination with the height-aware route grid, then reject map exits, enemies/NPCs, invalid heights and unknown collision. Do not teleport until position/state rollback and native actor update rules are understood. Later add an afterimage and Ninjutsu XP only when successful, verified and non-spammable. Save file unaffected.

## Test command

```sh
cc -std=c11 -Wall -Wextra -Werror -Isrc tests/som_disciplines_test.c -o /tmp/som_disciplines_test
/tmp/som_disciplines_test
make test-som-ai-proxy test-som-ai-player2
PYTHONPATH=tools python3 -m unittest discover -s tools -p 'test_som_*.py'
```

No Steam Link deployment should occur before build and hardware validation.

## Dodge roll from the native somersault (planned, waiting on a recording)

The player wants the game's own somersault animation used as a dodge roll.
It first appears later in the story (cannonball travel, or the rubber
ducky), so it cannot be recorded yet. When it comes up: send `pal_trace`
(records low WRAM, the palette shadow and the hero object 7E:E000-E1FF
every frame for 8 s, read-only) while it plays, then find the hero state,
animation, velocity and invulnerability fields that start and drive it.
Trigger it only through those verified fields, in verified field state,
toward collision-checked open ground (see the evade discovery steps above).

Availability (player's decision, 14 Nov): only these disciplines get the
dodge roll: swordsman (Swordsmanship), monk (Martial Arts), ninja
(Ninjutsu), and the not-yet-existing rogue, samurai and archer, which must
be added as disciplines first. Every other discipline has no dodge roll.

## Class magic, seals, evade and AI class choice (2026-11-14)

- **Spells per class** (`src/som_magic.h`): every discipline has its own list (38 of the
  42 spells; Analyzer, Revivifier and the undecoded 0x20/0x23 are left out). Tap **X** to
  cast the current one (cast on release), **hold X** to step to the next (every 0.5 s,
  name shown). Hostile spells (row byte 8 = 0x81) target the nearest live monster; ally
  (0x01) and self (0x00) spells target the boy (target 1). MP cost comes from the ROM;
  the boy starts with 0 MP, so use "Infinite MP (test)" until he has some.
- **Seals** (charge levels without holding): hold **R2**, tap the D-pad once per seal
  (Tiger/Boar/Dog/Ram; the D-pad does not move the hero meanwhile), then **B** (primary)
  or **Y** (secondary). Level = number of seals, capped at the weapon's learned level
  (`+0x19C`). No seals keeps R2+B = 1, R2+Y = 2. Once the swing ends the proxy sets the
  gauge word `+0x01A` = stage<<8 | 44 and `+0x19B` (som-decomp rom-combat.md 11.3),
  then releases B.
- **Evade**: **R1+A** plays the native back-roll pose 0x8D (55 px away from the facing,
  65 frames; hero-poses.md) with status 0x0400 (no hit resolved) for its length. Only
  disciplines with a dodge. A does not reach the game while R1 is held. Not yet checked
  live: whether the roll respects walls.
- **AI**: `tools/som_class_director.py` picks the class from weapon levels, build and MP
  (`class_<n>`); for grass it keeps the class, pulls the sword (`equip_sword`) to slash and
  puts the class weapon back 10 decisions later; it casts `cast_heal` /
  `cast_attack` from the class list, and uses `special_<n>` instead of holding for
  charged attacks.
- **Ninjutsu jutsu on R1+A**, by the weapon in hand: sword = **Shunshin** (native forward
  leap pose 0x9E, 85 px, untouchable while it plays); chakram/boomerang = **Kage Bunshin**
  (the back roll, then two cosmetic shadow clones 24 px either side for 6 s: copies of the
  boy's OAM entries, tiles 0x1C0-0x1D7 palette 1, written into free entries of the OAM
  shadow 7E:0800 before each frame; no objects, no hits).

## Network player 2 (2026-11-14)

- The core listens on **UDP 43931** (`src/som_net_pad.h`) and feeds SNES **pad 2**. Packets:
  `'S' '2' 1 0 buttons(LE16, Libretro ids) seq(LE16)`. Only 100.64.0.0/10 (Tailscale) and
  localhost; one player at a time (first sender, until 3 s quiet); buttons released after
  0.5 s of silence; Select never passes.
- Join page: `tools/som_remote_pad.py`, service `flxtr-som-remote-pad` on the PC, listening on
  its Tailscale address only: **http://100.69.7.81:43932/** (touch buttons, or any controller
  through the browser Gamepad API).
- The game still decides who pad 2 plays: the girl must be in the party and set to
  controller 2 in the game's own controller settings.
- **Colours for every class** (2026-11-14): Magic arcane violet, Divine Arts white/gold, Dark
  Arts black/crimson, Support & Dance rose, Weapon Flow teal/silver. With no class the shown
  palette is still held to the plain colours in field play.

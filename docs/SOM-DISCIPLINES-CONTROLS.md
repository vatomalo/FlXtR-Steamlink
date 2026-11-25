# SoM enhanced combat: disciplines, controls, and evade discovery

## Current controller layout (20 November 2026, supersedes earlier layouts)

| Physical input | Current behavior in active field play |
| --- | --- |
| **R2 (hold)** | **Block / guard**. The player's hero holds the game's own block pose and the proxy halves nonlethal HP loss that gets through; other heroes' poses do not change. Samurai Iai parry and Monk Counter Palm/Tiger Drop use the same guard. Samurai R2 + backward D-pad throws; Swordsman R2 + A bashes. |
| **L2 + D-pad signs, then B/Y** | Seals, charged specials and their level selection, moved from R2. L2+B is primary level 1 by default, L2+Y is secondary level 2. |
| **L2 + X** | Existing discipline moves (Issen, Hundred Fists, Whirlwind, Telekinesis, etc.) remain in place; not a style switch. |
| **L1 (tap)** | Toggle enemy lock-on. Only targeting, independent of B/Y. Native L1 is suppressed during active field play so it cannot open the wrong ring. |
| **R1 (tap)** | Cycle the currently selected class's **three FlXtR weapon styles** (fast -> balanced -> heavy -> fast). Native R1 no longer blocks. |
| **L3 + B** | Legacy L1+B style-cycle shortcut, moved to L3. B is suppressed so it cannot accidentally attack or start a combo. |
| **L3 held alone for 1 s, release** | Start AI mode. L2 and R2 holds never start it. |
| **R3** | Open the native ring. From the settings ring, highlight **Classes** and press B to open the party class editor. |
| **L2 + R1 / L2 + L1** | Next / previous class for the boy (all 16, wrapping). L1 alone still locks on. |

Outside active field play, the base game's menu buttons remain unchanged.
During the AI gambit menu, L1/R1 still turn pages and do not change styles.

**Weapon names:** the custom HUD under the class title shows **only FlXtR's
own weapon/style names**: `Katana`, `Dai Katana`, `Yumi`, `Crossbow`, etc.
It no longer displays compound vanilla names like `Masamune Katana`.
Native in-ROM inventory/ring names are unchanged, since modifying them
requires separate ROM text patching and validation.

Previous controller descriptions below are historical and may refer to
the old R1 block, R2 seals, and L1+B style bindings.


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
- **Testing without MP:** turn the AI on (hold L3 1 s), press a button
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

- Press Start during manual play and open the Classes icon from the settings ring. Up/Down highlights a class, B equips it, and A closes the editor.
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
- The party editor opens from the Classes icon in the settings ring during manual play.
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
| L3 long hold | Existing AI on/off toggle | Preserve |
| L/R in AI overlay | Gambit pages | Implemented |

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

## Cartwheel evade (monk and rogue, 2026-11-18)

Martial Arts and Roguecraft evade with a cartwheel; the ninja keeps his
evade and jutsu. The hero is hidden (object +0x0E bit 7) and the proxy draws
him turning over the move: his standing frame turned 0, 90, 180, 270
degrees and upright again (`tools/som_cartwheel.py`, pack
`som_cartwheel_frames.bin` in cores/experimental, npc_work format, kind
0x80-0x82; sideways uses the front frame, up/down the side frame), in his
live palette, so the class colours stay. 4 px x 16 frames (64 px), spin 25
frames. Without the pack it is the same move, drawn as the plain sprite.

Camera: every dodge now moves the camera with the hero. The game scrolls
only by the hero's own walking steps, so a proxy-carried hero left the
camera behind (offline replay of a field state: 48 px hop, camera 7E:00A8
unmoved). The camera words move together with their BG scroll words:
A8/B0 and AA/B2 (BG1), AC/B4 and AE/B6 (BG2), clamped to the map size
C0/C2; new tile columns stream in on their own (checked at 4 px per frame).

ROM frames found on the way (boy, pose table D0:FFF0): handstand 176-179
(one picture), curled tumble 134-137 (end of pose 30), ball and handstand in
pose 34 (likely the cannonball launch), side kick 89 (glove attack 2),
airborne kicks 92, 166, 167.

## Flying kick (Martial Arts, X/Triangle, 2026-11-18)

X (Triangle) no longer casts for Martial Arts: it is the flying kick. It is
the swordsman's R1+A bash with the glove: the game's own run (A held, up to
14 frames, stops at a wall or next to a monster), A released, then the
game's glove attack (B) at a full gauge, which counts as a combo hit. With
a combo level ready (3+ hits) the landing kick is the combo finisher instead:
the glove's charged attack at that level (capped at what it learned).
While it plays the hero is hidden and drawn as the glove's own kick (pose
table frames: side 88 -> 89, down 90 -> 91, up 92 -> 93) in a 12 px jump
arc over the run, landing at the strike (pack kinds 0x90-0x92 in
som_cartwheel_frames.bin). Spells stay in the ring menu.

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
- **Seals** (charge levels without holding): hold **L2**, tap the D-pad once per seal
  (Tiger/Boar/Dog/Ram; the D-pad does not move the hero meanwhile), then **B** (primary)
  or **Y** (secondary). Level = number of seals, capped at the weapon's learned level
  (`+0x19C`). No seals keeps L2+B = 1, L2+Y = 2. Once the swing ends the proxy sets the
  gauge word `+0x01A` = stage<<8 | 44 and `+0x19B` (som-decomp rom-combat.md 11.3),
  then releases B.
- **Evade**: **A** (R1+A too) plays the native back-roll pose 0x8D (55 px away from the facing,
  65 frames; hero-poses.md) with status 0x0400 (no hit resolved) for its length. Only
  disciplines with a dodge; for them A no longer dashes (dash to be re-added), other classes
  keep the native A. The D-pad is hidden from the game while the pose plays (a held
  direction made the game replace Shunshin with walking, live) and the pose's busy flag
  `+0x42` is cleared if the game cut the pose short. Not yet checked live: walls.
- **AI**: `tools/som_class_director.py` picks the class from weapon levels, build and MP
  (`class_<n>`); for grass it keeps the class, pulls the sword (`equip_sword`) to slash and
  puts the class weapon back 10 decisions later; it casts `cast_heal` /
  `cast_attack` from the class list, and uses `special_<n>` instead of holding for
  charged attacks.
- **Ninjutsu jutsu on A**, by the weapon in hand: sword = **Shunshin** (native forward
  leap pose 0x9E, 85 px, untouchable while it plays); shuriken/boomerang = **Kage Bunshin**
  (the back roll, then two cosmetic shadow clones 24 px either side for 6 s: copies of the
  boy's OAM entries, tiles 0x1C0-0x1D7 palette 1, written into free entries of the OAM
  shadow 7E:0800 before each frame, two frames on and two off for a ghostly look, since
  palette 1 cannot blend; no objects, no hits).

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

## AI weapon alternation (2026-11-15)

`tools/som_weapon_tactics.py` picks between the class's two weapons and prints why
(`[weapon] sword -> bow: enemy 60 px away: the bow reaches 70 px, the sword only 28`):
whip blind spot under ~18 px, low HP (<40 %) fights from the longer reach, an enemy beyond
melee reach but inside the other weapon's, the higher-level weapon at close range, and
training the weaker weapon when it lags two levels behind and no enemy is near. 4 s
cooldown, never mid-swing. The planner strikes from each weapon's reach
(`WEAPON_REACH_PX`, from som-decomp weapons-ranged.md 2.4) instead of sword distance.

## Steam Link CPU (2026-11-15)

Single core, 93 % busy while playing (RetroArch 55 %, kernel/network 37 %, Steam Link
stack 13 %, state_guard 7 %). Changes:
- **Music not synthesised**: our own snes9x2005_plus build (`scripts/build-cores.sh
  snes-plus` applies `patches/snes9x2005-flxtr-music-voices.patch`) installed as
  `cores/experimental/snes9x2005_plus_flxtr_libretro.so`; the proxy prefers it and calls
  `retro_flxtr_skip_music_voices(2)`: voices 0-5 never mixed, voices 6-7 (all effects,
  som-decomp audio.md) only for 2.5 s after an effect request (APU port 0 = 2). Headless
  x86 benchmark, 3600 intro frames: 1.46 s -> 1.11 s (-24 %); mode 0 is identical to the
  stock core; all sound off would be 1.01 s. The current filter uses effect key-on
  ownership as well as the voice mask to keep native music out of the mix.
- RetroArch resampler: **keep `sinc`**. The Steam Link's RetroArch 1.6.7 has only `sinc`
  and `nearest`; setting `cc` made every launch die right after startup (2026-11-15).
- Measured on the Steam Link (600 intro frames, headless): stock core 4.08 s, our core
  mode 0 4.04 s, mode 2 3.14 s (-23 %).
- Song names: the proxy shows "Now playing" when 7E:1E05 changes (`src/som_songs.h`; 13
  decomp-verified names, others "Song 0xNN" until named in
  `cores/experimental/som_song_names.tsv`, lines `<hex id><TAB><name>`).
- state_guard rescans state folders every 10 s and snapshots each stable version once.
- Proxy exports one atomic WRAM snapshot at 2 Hz unless the AI plays (10 Hz). The
  observation server sends deltas from that snapshot. Legacy hot-range requests
  extract the same ranges on demand.

## Gambit pages and situational classes (2026-11-15)

- On the Actions page, **AI debug messages** defaults to off. Turning it on shows the
  bridge's blue `AI:` status banners and class-selection debug bubbles; contextual AI
  speech bubbles remain enabled either way. Player 2 can open and operate the Gambit
  menu from controller 2 while AI play is active, including **Exit gambits: you play**.
- The AI overlay now has three pages cycled with **L1/R1**: **Gambits: Actions** (the six
  activities, file `som_ai_gambits`, unchanged format), **Gambits: Combat** (file
  `som_ai_combat`, 8 digits: press the attack, keep distance, guard and counter, evade,
  charged attacks, cast magic, switch classes, switch weapons; default `32223333`) and
  Disciplines.
- The bridge reads the combat page (`gmc`) every 5 decisions: press vs distance sets the
  planner's preferred tactic, guard 0 drops planned blocks, evade > guard turns a planned
  block into the class's evade/jutsu (`evade` command), charge 0 disables charged attacks,
  magic sets the cast interval (14 s / weight; attack spells only for classes that have
  one), classes sets the class cooldown (150 s / weight, 0 = never), weapons the weapon
  cooldown (12 s / weight).
- `som_class_director` now scores advantages for the moment: hurt -> Divine Arts / Support
  (with MP), crowd -> Magic / Ninjutsu / Support, far enemy -> Archery / Magic, tough enemy
  (max HP >= 200) -> Bushido / Swordsmanship / Dark Arts / Roguecraft, one close enemy ->
  Martial Arts, calm -> Weapon Flow. Margin 1, no switching with an enemy within 40 px.
  Each switch shows a class line ("Shadows, hide me!") and the reason.
- Exact music skip: the core reads the SPC700 driver's effect-owned voice mask `$AE` and
  synthesises only those voices (5-7), with a 1/6 s release tail; x86 1.48 s -> 1.15 s.

## Combo ladder, swordsman bash, real shadow clones (2026-11-15)

- **Combo ladder** (`src/som_combo.h`): only confirmed hits count (a monster's HP drops
  within 75 frames of the swing). 3 hits = level 1, then +1 level every 2 hits; each
  weapon switch that lands (B primary <-> Y secondary) adds +1 (at most +2); max 8. A whiff
  or a 3 s pause drops the chain (not while B/Y is held). A counter ("5 HIT MIX LV3") shows
  above the hero. **Hold B or Y for 12 frames** with a level ready: that button's weapon
  charges straight to the level (capped at what it learned, via `som_special_start`) and
  fires as the game's own power attack. Manual play only. Replaces the old fixed
  4-hit level-1 finisher.
- **Swordsman R2+A bash**: the game's own run (A held, 3 px/frame, stops at walls) for up
  to 14 frames (~42 px) or until a monster is within 22 px, one frame of release, then
  the game's own sword swing (B) with the run gauge `E1ED` cleared so it hits at full.
  R1/D-pad/Y/X are hidden from the game meanwhile. Refused while `E1ED` is still loading.
- **Kage Bunshin** (ninja + shuriken, A; `src/som_bunshin.h`, `src/som_bunshin_draw.h`):
  the old clones were written into the OAM shadow before the frame and the game rebuilt
  OAM over them, so they never showed. Clones are now positions the proxy keeps (two,
  across the facing, open ground only, 10 s), copying each hero step per axis and stopped
  by walls (tile attributes as `tools/som_memory.py`, anything uncertain = wall). They are
  drawn on the frame copy from the boy's OAM pieces decoded from VRAM with palette
  7E:0720, so they copy his pose and class colours. A monster touching a clone makes it
  explode (`10 + Ninjutsu level / 2` damage via `+0x1F1` to every monster within 40x32 px).
  A monster touching the hero swaps him into the clone farthest from it (Kawarimi) and the
  spot he left explodes. A with clones out swaps into the clone the D-pad points at
  (the clone takes his old spot). Swaps write `E002/E004` directly: **not yet checked live**
  (camera, party followers, layer edges).

Tests: `make test-som-combo test-som-ai-proxy`.
- **Hop dodge for monk, rogue and archer** (A): the game's 20-frame hop pose `0x95`
  instead of the slow 65-frame back roll (`0x8D`, really the knocked-down pose, kept for
  swordsman, ninja and samurai). `0x95` does not move the hero by itself (hero-poses.md),
  so the proxy moves him 18 frames before each emulated frame: monk/rogue 2 px (~36 px),
  archer 3 px (~54 px), toward the held D-pad direction or straight back from the facing,
  per axis onto open ground only (same wall test as the clones); a wall ends the travel.
  Untouchable (status 0x0400) while the pose plays. Not yet checked live.

## Auto save state on exit (2026-11-15)

Every `.state.auto` on the Steam Link (all SNES games) was exactly 4096 bytes and never
loaded. RetroArch 1.7.6 writes savestates as a task, 4096 bytes per task tick
(`tasks/task_save.c`, `SAVE_STATE_CHUNK`), and quits right after queueing the exit
auto-save, so only the first chunk lands. The cores serialize fine (531668 bytes,
round trip verified headless on the Steam Link). Fix for games run through the proxy: in
`retro_unload_game` the proxy serializes the whole state, writes
`<rom>.state.auto.flxtr-full`, and a detached helper renames it over `<rom>.state.auto`
once RetroArch has exited (at most 30 s wait). `FLXTR_SOM_AUTO_STATE=0` disables it.
Other games/cores still get the truncated file (needs a RetroArch-side fix).

## Dodges are proxy-driven (2026-11-15, replaces the pose-based evade)

A headless run of the real ROM on the Steam Link showed that the game's poses written as
`+0x1C=0x40 / +0x11=code / +0x42=1` do not play in field play: back roll `0x8D`, leap
`0x9E` and hop `0x95` ended within 6 frames without moving (their scripts run under the
event VM, `$D0 != 0`). The old evade, Shunshin and hop were therefore no-ops live.
Now (`som_disc_dodge`): the proxy carries the hero per frame onto open ground (wall test
of the clones), keeps status 0x0400 (untouchable) for the move plus 8 frames, hides
A/D-pad/B/Y/X from the game meanwhile and draws an after-image trail (his own sprite, 4
and 8 frames behind, every other frame). Swordsman/samurai 3 px x 16 (48 px) back, monk/
rogue cartwheel 4 x 16 (64 px, see below), archer 3 x 18 (54 px), ninja with sword = Shunshin 6 x 12 (72 px)
forward, other ninja weapons 3 x 16. A held direction aims any of them.
Verified headless on the Steam Link: 48 px roll with status 0400 set then cleared,
Shunshin stopped by a wall, trail drawn.

### Jutsu buttons (2026-11-19)

- **Hold A / Circle** (20 frames, ninja only; Kawarimi is a ninja skill): the tap's hop still fires on
  the press; the held request then arms **Kawarimi no Jutsu** as soon as the hero is free
  (waits up to 90 frames, even if A is let go). Armed for 3 s, it fires when a monster
  touches or hits him. Log: `[som-ai] KAWARIMI from held A`.
- **L2 + X / Triangle** (ninja with a short-range weapon in hand): **Shunshin no Jutsu**,
  a clone on each side across the facing that explodes when a monster reaches him. The
  game never sees that X. Any other class or weapon: L2+X still reaches the game.
  Log: `[som-ai] SHUNSHIN from L2+X`.
- The AI keeps its own commands `kawarimi` / `shunshin`.

## Learning by doing: discipline XP (Phase B, 2026-11-15)

Until now `som_disc_award` had no caller: no discipline ever gained XP. `src/som_xp.h` and
`som_xp_give` in the proxy now award XP only for events confirmed in WRAM:

| event (verified how) | trains | base XP |
|---|---|---|
| weapon hit: a live monster lost HP within 75 frames of the swing | the class if it uses that weapon, else the weapon's home (glove/spear Martial Arts, sword/axe Swordsmanship, whip Support & Dance, bow/javelin Archery, boomerang/chakram Ninjutsu) | 10 |
| that hit switched weapons mid-chain | Weapon Flow | 8 |
| dodge with a monster within 24 px (near miss), once per dodge | the dodging class | 15 |
| hostile spell whose target lost HP within 240 frames | the casting class, else Magic | 15 |
| shadow clone explosion that caught a monster | Ninjutsu | 20 |
| familiar strike | Summoning | 4 |

Diminishing returns: all awards against one monster species shrink as 8 / (8 + recent
count) and stop at 64; a minute without that species resets it. Empty swings, invulnerable
targets and button mashing give nothing. Level-ups show a bubble and save
`som_ai_disciplines` at once; other progress is saved at most once a minute and on exit.
Levels are per device, not per save slot (unchanged file format v1). Not yet trained:
blocking (no verified "intercepted hit" signal yet), stealth (no detection model), Divine/
Dark Arts and Support beyond their weapons/spells.

## HUD class label and barrel-free dodge (2026-11-15)

- The class name is drawn right of the boy's HUD portrait, under the HP numbers
  (`som_hud_portrait`: OBJ palette 1, tile 0x1E8-0x1EF, low on screen; seen at x 88, y 199
  with the boy alone). No portrait on screen (HUD hidden, menus, events): no label.
  Party layouts with the girl/sprite are not yet checked.
- Dodges no longer use status 0x0400: that is the Barrel status (item 10 toggles it) and
  drew the hero as a barrel. Instead, before and after every frame of the dodge the hit mask
  `E059` and pending damage `E1F1/E1F2` are cleared and any HP lost is put back.
  Verified headless: no barrel; the hit-blocking path is unit-tested only (no monsters
  in the test save).

## Combat verification against real monsters (headless, real ROM, 2026-11-15)

Headless = the real Turbo ROM on the Steam Link, scripted input, a disposable copy of the
save (`tools/som_headless`). Fixture: `to_rabites.txt` walks from the Potos inn save to map
130 (three Rabites, 40 HP) and saves `rabites.state` outside Git
(`~/.local/share/flxtr/som-fixtures/`). Rabites are moved next to the hero with the
harness `monster` command (test copy only).

| scenario | verified (headless) |
|---|---|
| `combat_rabites.txt` | near-miss dodge XP (+15); weapon-hit XP fading 8, 8, 7, 6 on one species; combo 3 -> level 1, 4 hits; holding B fired the finisher through `som_special_start` (sword level 1 charged and released) |
| `clones_rabites.txt` | 2 clones; a Rabite touching one -> explosion hit 1 monster, Ninjutsu XP; a Rabite on the hero -> substitution; no sprite corruption in the frames |
| `dodge.txt`, `no_free_xp.txt` | 48 px dodge, no Barrel bit; no XP for swings/dodges with no monster |

Not covered headless: whether a real monster attack lands during a dodge (Rabite timing is
not scripted; the shield path is unit-tested), camera/followers after a swap, Turbo
specials above level 1. Hardware procedure (2 minutes, ask the player): load the save, walk
to the Rabites west of Potos, as Swordsman hit one Rabite three times then hold B (expect
"N-hit combo / level 1 finisher"); stand still until a Rabite lunges and press A just before
it lands (expect no HP loss); as Ninja with the shuriken press A and let a Rabite walk into a
clone (expect "Bunshin Daibakuha!"). Then copy `/home/apps/greenlink/game.log` lines
`[som-ai] XP|COMBO|EVADE|BUNSHIN` into this section as hardware results.

## Progression per save (format v2, 2026-11-15)

`src/som_progress.h` + `som_prog_*` in the proxy. Secret of Mana Turbo keeps four 0x400-byte
save slots at SRAM 0x0000 (mirror at 0x1000); a slot is a copy of WRAM (slot+0x004 =
7E:CC01.., slot+0x104 = hero records), measured on the player's save. Right after a load or
a save, 7E:CC01..CC40 equals exactly one slot.

- Identity: FNV-1a 64 of the whole slot block. When the game writes a slot that matches the
  game in play, the progression (levels, XP, chosen class, reserved perk bits, ROM hash) is
  written to `som_progress/<fingerprint>.prog` (text, checksum line; temp + fsync + rename;
  the previous generation kept as `.prev`; a record of an unknown version is never
  overwritten; a damaged record falls back to `.prev`).
- Entering field play from a load restores the record of that exact block; no record (an
  overwritten or never-recorded slot) or no matching slot on the first field entry (new game)
  = fresh progression. Nothing is inherited across playthroughs. SRAM is only read.
- Save states (including the exit auto-state) carry the progression in a 1036-byte trailer
  (`SOMPROG2`): state size is now core + 1036 (532704). States of the core's exact size
  (older ones) still load and keep the progression in memory.
- Legacy: the old device-wide `som_ai_disciplines` levels are copied once to
  `som_progress/legacy-v1.txt` and applied to the save in play only by the tool command
  `progress_claim_legacy` (higher levels win), then renamed `.claimed`. The v1 file keeps
  the session's class choice as before.
- Headless on the real ROM: slot 1 load detected (fresh: no record yet), 532704-byte state
  round trip restores it, the older 531668-byte fixture still loads. Hardware: pending; to
  carry the existing live XP over, run `progress_claim_legacy` once with the save loaded.

## Perks (2026-11-15)

`src/som_perks.h`: a table of perks (stable id, discipline or "the chosen class", minimum
level, levels per rank, max rank, effect, per-rank amount, cap, optional second discipline +
level as prerequisite for future synergies). Ranks derive from verified levels only, so
they follow the save (progression v2 stores the unlocked bits too) and cannot stack from
duplicate events. `FLXTR_SOM_PERKS=0` (read at game load) turns every effect off.

| perk | trains from | effect (per rank, cap) |
|---|---|---|
| Dodge Mastery | the class, from level 5, every 5 | +2 movement frames (cap +6); not the untouchable grace |
| Clone Mastery | Ninjutsu, from 5, every 5 | +4 clone explosion damage (cap +12); clone count unchanged (no new buffers) |
| Finisher Mastery | the class, from 3, every 5 | +1 finisher level (cap +2), only on an existing chain; the game still caps it at the weapon's learned level |
| Weapon Flow | Weapon Flow, from 5, every 5 | +1 weapon-switch level counted (cap +2); switches only count when both hits land within the chain window |

Unlocks log `[som-ai] PERK unlocked: <name>`. The Shunshin follow-up synergy is not built
(the table supports it; see the test `Shadow Strike` row).

### Casters trade weapons for spells (2026-11-20)

Magic, Divine Arts, Dark Arts, Summoning and Support & Dance (`som_disc_caster`):
**B** casts the class's first spell, **Y** the second, **X** the rest of the list
(tap: cast, hold: next). The game never sees those B/Y presses. With too little MP
for the B spell, B is the game's weapon swing again, so he can always attack.
Log: `[som-ai] CASTER B|Y: <spell>`.

| Class | B | Y | X |
|---|---|---|---|
| Magic | Fire Ball | Freeze | Thunderbolt, Earth Slide, Gem Missile, ... |
| Divine Arts | Lucent Beam | Cure Water | Remedy, Lucid Barrier, Light Saber, Wall |
| Dark Arts | Dark Force | Energy Absorb | Evil Gate, Dispel Magic, Magic Absorb, Change Form |
| Summoning | Burst | Sleep Flower | Lunar Magic, Moon Energy |
| Support & Dance | Speed Down | Cure Water | Speed Up, Defender, Silence, Balloon |

### Samurai kit (2026-11-20)

- **Issen (L2 + X):** a straight dash-cut along the facing, 6 px x 14 frames (~84 px),
  untouchable like the dodge; every monster passed is cut once (16 + 2 x Bushido level).
  4 s cooldown. Log `[som-ai] ISSEN ...`.
- **Iai counter (guard with R2):** a hit landing on the guard is parried (no damage) and
  answered with a cut on the attacker (20 + 2 x Bushido level), on the Iaijutsu cooldown.
  Log `[som-ai] IAI COUNTER ...`.
- **Zanshin:** a kill readies Iaijutsu at once, so kills chain.

### Martial Arts kit (2026-11-20)

- **Hundred Fists (L2 + X):** for 1 s the game sees B every 4 frames (his own punches)
  and every 8 frames each monster just in front takes 3 + level/2 more. 5 s cooldown.
- **Counter palm (guard with R2):** a hit on the guard is parried and answered
  (14 + 2 x Martial level), every 2 s at most.
- **Rising Dragon:** every third hit of a combo adds a strike (8 + level).
- Not built: cartwheel into a kick (the cartwheel is the game's own pose).

### Swordsman, Rogue and Archer on L2 + X (2026-11-20)

- **Whirlwind (swordsman):** a spin cutting every monster in the blast box around him
  (14 + 2 x Swordsmanship level). 4 s cooldown.
- **Shadowstep (rogue):** Blink's hidden travel to 24 px behind the nearest monster
  (within 128 px, the far side seen from the hero); the next hit within 2.5 s is a
  **backstab** (+12 + 2 x Roguecraft level). 5 s cooldown.
- **Volley (archer):** three arrows in a cone: every monster up to 128 px ahead and within
  16 + distance/3 px of the line takes 10 + Archery level. 3 s cooldown.
- An L2+X release is never read as a plain X release (no stray cast or Iaijutsu).

### Class weapons: real game weapons, class stats (2026-11-20)

Each class's weapons are real game weapons pinned at a fixed forge grade (their art and
the ROM's name, shown under the class title on the HUD with the style after it), with
stats set by the class, not by the grade. The B weapon keeps its styles (power, speed,
reach: Tanto / Katana / Dai Katana for the samurai's Masamune). The Y weapon (and
Weapon Flow's X bow) hits like its type's tier-0 row at class level 1 rising to its top
plain row at class level 8, times the class's power share (`som_class_arms`).

| Class | B | Y | X |
|---|---|---|---|
| Swordsman | Excalibur | Gold Axe | |
| Monk | Hyper Fist | Coral Spear (90 %) | |
| Ninja | Kusanagi | Shuriken | |
| Mage | bare hands (glove, 70 %) | bare hands | |
| Cleric | bare hands | bare hands | |
| Warlock | bare hands | bare hands | |
| Summoner | bare hands | bare hands | |
| Dancer | bare hands | bare hands | |
| Weaponmaster | Orchicalcum | Halberd | Wingbow |
| Rogue | Kusanagi | Longbow (95 %) | |
| Samurai | Masamune (110 %) | Wingbow (85 %) | |
| Archer | Garuda Buster (115 %) | Valkyrian (110 %) | |

The pinned grade replaces whatever grade the weapon had (orb forging no longer changes
what a class weapon hits for). Grass: the AI already takes the sword out to cut and puts
the class weapon back after (`[grass] sword out`).

### Four new classes (2026-11-20)

| # | Class (title) | B / Y | X | L2 + X | A |
|---|---|---|---|---|---|
| 12 | Vampire Hunting (Hunter) | whip (Morning Star) / thrown cross (Silver Boomerang) | **Holy Water**: a flask 40 px ahead burns 1.5 s, every monster on it takes 5 + level every 15 frames (4 s) | **Grand Cross**: every monster within 128 px takes 20 + 3 x level (20 s) | Backflip (56 px) |
| 13 | Necromancy (Necromancer) | casts Energy Absorb / Evil Gate (bare hands) | **Raise Dead**: within 10 s of a kill, a monster rises as his familiar (15 s) | - | native |
| 14 | Artifice (Artificer) | hammer (Werebuster) / crossbow (Great Bow) | **Bomb**: set 24 px ahead, goes off after 1 s (18 + 2 x level, blast box) (3 s) | **Mine**: up to 3 where he stands, a monster stepping near sets one off (24 + 2 x level) | Evade |
| 15 | Psionics (Mentalist) | casts Sleep Flower / Balloon (bare hands) | **Mind Control** on X (nearest ordinary enemy in 96 px, 20 s, 30 s cooldown). **Telekinesis** on L2+X pulls a native dropped treasure chest (monster type 0x56, slots 3..5) from up to 160 px to interaction distance, with wall checks and 4 s cooldown. | - | native |

Saved data: progress records and style files written with 12 classes still load (the new
classes start at level 1); the style file is now `s2 <classes> ...`.

### 2026-11-20 changes

- Kawarimi (hold A, AI `kawarimi`) is a ninja skill only.
- Samurai: A with a monster in grab reach grabs and throws it; otherwise A is the evade roll.
- Swordsman: A is the cartwheel (like the monk and the rogue), no longer the evade roll.
- The HUD weapon name is centred under the class title.
- Ninja (with sword) and Samurai: A is **Ukemi**, the ROM-derived somersault
  frame overlay (native pose script 34, kind 0xC0..0xC2) accompanying a collision-checked 54 px
  carried dodge. The direction held chooses travel; with no direction it rolls
  back relative to the current facing. The native 0x93 cartwheel is not used
  for Ukemi. Install the regenerated som_cartwheel_frames.bin to see it.

### Lock-on targeting (2026-11-20)

Tap **L1** (without B or Y in between) to lock onto the nearest monster within 160 px; a
yellow "V" bobs over it. While locked the hero keeps facing it (before and after each game
frame), so walking strafes around it and every attack goes its way. Tap L1 again to let go;
the lock also drops when the monster dies, gets 220 px away, or the map changes. R1 taps now cycle the styles; L3+B is the legacy shortcut. Player only (the AI does not use it). Log: `[som-ai] LOCK ...`.

### Summon graphics, Ukemi and Mentalist telekinesis (20 November 2026)

- **Summons/familiars** keep the current map's actor-specific sprite slots and palettes. The live monster's VRAM art is captured **before** cloning; when it follows between maps the art is remapped into the destination's native OBJ tiles. A reused slot never writes the source map's raw VRAM tile numbers into other actors' artwork, and shared palettes are not repainted. Periodic familiar-state saves preserve the captured art rather than replacing it with foreign-map upload buffers. Legacy `som_summon.bin` packages are supported through the slot-local tile buffer, but do not contain a complete original VRAM snapshot and still require visual confirmation on hardware.
- **Ukemi** is a somersault with ROM-derived hero frames (native cannon-flight pose 34, kind `0xC0` boy, `0xC1` girl, `0xC2` sprite) generated by `tools/som_cartwheel.py`. Ninja/Samurai **A** still moves a collision-checked 3 px x 18 frames, but the proxy draws the tumble and temporarily hides the normal hero sprite instead of relying on the game accepting reaction `0x13`. The Monk, Rogue and Swordsman still use the native cartwheel on combat maps. Regenerate the frame pack to see the new animation.
- **Mentalist (Psionics):** **X** continues to mind-control the nearest ordinary monster. **L2+X** (or AI command `telekinesis` / `pull_chest`) first tries to blink an endangered ally to a clear spot near the caster. It can only choose an open landing farther from enemies and on the same floor. AI Mentalists do this for injured allies when they have enough energy. When there is no safe rescue, the button acquires the nearest active native dropped treasure chest (monster type 0x56), on the same floor and within 160 px and pulls it at 3 px/frame toward the hero until 28 px away. Four seconds cooldown, maximum 90 frames, map/event/collision/actor-identity guards. Loot and traps still use the original game's chest-opening logic. Static dungeon/event chests, crystal orbs, other props and NPCs are deliberately excluded until their actor types are verified.

To stage a fresh Steam Link ROM-derived move pack while building the experimental proxy:

```sh
SOM_ROM_PATH="/path/to/your/Secret of Mana, Turbo.smc" scripts/build-som-ai-proxy.sh
# Stage/copy build/som-ai/som_ai_proxy_libretro.so and
# build/som-ai/som_cartwheel_frames.bin to Steam Link
# /home/apps/retroarch/cores/experimental/
```

The ROM is only read for frame extraction and is never bundled. Unit targets:
`make test-som-telekinesis test-som-familiar-art test-som-ai-proxy test-som-raid`.

### MUSIC page: FlXtR's R2 music menu in the game (2026-11-20)

Start opens the menu; **L1/R1** switch between CLASSES and **MUSIC**. On MUSIC:
- **Search**: B opens the on-screen keyboard (D-pad moves, B types, Y deletes, X searches,
  A goes back); results fill the list.
- **Radio**: Left/Right pick one of FlXtR's 16 MTV genre stations, B tunes it: a fresh
  shuffled list that plays track after track (the next one decodes while one plays).
- **Stop**: the game's soundtrack covers come back.
- A result row + B plays it; X skips to the next track; A closes the menu.

The page writes one request line to `/tmp/flxtr-som-jukebox.req`; the Steam Link's music
bridge (`tools/som_music_bridge.py`, uses FlXtR's `tools/music.py`) answers in
`/tmp/flxtr-som-jukebox.list` and mixes the track into the game's audio like the covers.
While the jukebox plays, the game's song changes are ignored.

### Bushido weapon masks and SD3 dagger import (experimental, 20 November 2026)

The old all-pixel 55%/150%/200% scaled-art renderer was replaced on the
`feature/som-bushido-blade-masks-20261120` branch. `scale_pct` remains
in the style data for existing balancing/configuration compatibility, but
the proxy **does not scale the entire weapon** any more.

The pack carries Bushido's blade masks and the Ninjutsu Kusarigama hybrid:

- **Where the art comes from (2026-11-25):** the core makes the sword looks and
  hero moves from the running ROM at game load (`src/som_art_gen.h`, about half a second of CPU
  on the Steam Link), on Steam Link and Android alike; no art file is installed.
  `som_weapon_masks_custom.bin` (your own dagger) wins; the file
  `som_ai_weapon_masks_off` turns the looks off.
- **Tanto / Katana / Dai Katana (2026-11-25):** drawn in the sword's own
  palette and outline at its hilt and angle (`tools/som_katana_art.py`): 0.42,
  1.15 and 1.75 times the native blade. Samurai, all three styles;
  Ninja: Kunai in the fast style, else Ninjato. Hunter (close hits only, the
  whip stays): wooden stake, Silver Dagger, Kukri. Classless: each forged sword has its own look. Swordsman: Rapier, Longsword,
  Claymore; Weapon Flow: Zweihander, Greatsword, Colossus, and a
  giant axe and spear (the game's own, doubled); Rogue: Dagger,
  Cutlass, Estoc. Boy, girl and sprite alike. The slash
  trail stays native. A Tanto may instead be a user-extracted Seiken Densetsu 3 dagger PNG. The PNG must have a transparent
  background and be cropped to just the dagger.
- **Kusarigama (Ninjutsu heavy style):** normal B taps still use the whip. Hold
  B after its recovery to charge the sword's native level-2 attack; the
  level-2 body animation is paired with whip frames extracted from the local
  ROM. It requires sword charge level 2 and the opted-in weapon-mask pack.
- The runtime draws palette indices 1..5 opaque; separate FX indices 6..10
  have 25% translucency on the post-core frame, including RGB565, 1555, 8888.
- Native game/hero palettes and attack scripts remain unchanged. The proxy
  restores native weapon tiles if a style is off, its frame is missing, or the
  user has not opted in. **This is still experimental:** animation alignment
  needs comparison with live recorded Steam Link frames before deployment.

Local build with a ROM already available on SkyLoft:

```sh
SOM_ROM_PATH="/path/to/Secret of Mana, Turbo.smc" bash scripts/build-som-ai-proxy.sh
```

Optionally include a locally extracted dagger image. The `X,Y` points are
pixels relative to the **cropped transparent image**, on the grip and blade
tip respectively:

```sh
SOM_ROM_PATH="/path/to/Secret of Mana, Turbo.smc" \\
SOM_DAGGER_PNG="/path/to/my-extracted-sd3-dagger.png" \\
SOM_DAGGER_HILT="4,13" SOM_DAGGER_TIP="23,3" \\
bash scripts/build-som-ai-proxy.sh
```

*Replace those illustrative coordinates with pixels from your actual PNG.*
The project does not distribute donor graphics or game ROMs.

Review `build/som-ai/som_weapon_preview.png` plus its `.txt` report before
installing anything. The contact sheet places Tanto, native Katana and Dai
Katana in three rows, with native swing frames in columns and a red marker
on any missing/unsafe mask that falls back to the original weapon. It can
also be regenerated directly:

```sh
python3 tools/som_weapon_preview.py "$SOM_ROM_PATH" \\
  build/som-ai/som_weapon_masks.bin build/som-ai/som_weapon_preview.png
```

Review the preview before deployment. `bash scripts/deploy-som-arm-performance.sh
proxy` installs the pack and creates `som_ai_weapon_masks_on`; keep
RetroArch/Secret of Mana closed for the install. The deployment script backs
up the previous core, pack, and marker for rollback.

### Casters' spell sets and class spell names (2026-11-20)

Casters (Magic, Divine Arts, Dark Arts, Summoning, Support & Dance, Necromancy, Psionics)
hold no weapon, so their three styles are **spell sets**, cycled with the style button
(R1): set I (the default) puts the class's spells 1-2 on B and Y, set II spells 3-4, set
III spells 5-6 (wrapping); X cycles the rest. The HUD shows the set's B / Y spells.

Each class names its spells its own way (`som_class_spell_names` in src/som_magic.h), e.g.
Ninjutsu: Katon (Fire Ball), Hyoton (Freeze), Futon (Air Blast), Nemuri (Sleep Flower);
Divine Arts: Holy Light (Lucent Beam), Healing Prayer (Cure Water); Dark Arts: Shadow Bolt
(Dark Force), Life Siphon (Energy Absorb); Summoning: Spirit Burst, Dream Bloom; Support &
Dance: Slow Waltz, Quickstep; Necromancy: Soul Drain, Grave Gate; Psionics: Mind Sleep,
Levitate; Archery: Wind / Thunder / Stone Arrow; sabers: Flame / Thunder / Frost Edge.
Magic keeps the game's names.

### X binds, X again releases (2026-11-20)

- **Summoning: X summons** a familiar (a cloned monster fighting for the hero);
  **Dark Arts: X makes a Shadow Pact** (the same, a dark familiar).
- Necromancy: X raises the dead (after a kill); Psionics: X takes over a mind.
- For all four, **X again while one is bound releases it** at once (the mind-controlled
  monster is let go, a familiar is dismissed). Their extra spells are on the B/Y spell sets.

### Ninjutsu X then A: Blink (2026-11-20)

Ninja Storm style: a tapped X waits a quarter second before the jutsu goes off. Press **A in
that wait** and the jutsu becomes **Blink**, the higher evade: up to 96 px toward the held
direction (else the facing), stopping before any wall. Blink costs 10 Chakra and 2 AP, and
has its 10 s cooldown.

### FlXtR MP and AP (2026-11-20)

Every class has its own energy, named its own way: Spirit (Swordsmanship), Chi (Martial
Arts), Chakra (Ninjutsu), Mana (Magic), Faith (Divine Arts), Malice (Dark Arts), Bond
(Summoning), Rhythm (Support & Dance), Flow (Weapon Flow), Nerve (Roguecraft), Ki (Bushido),
Focus (Archery), Zeal (Vampire Hunting), Souls (Necromancy), Steam (Artifice), Psi (Psionics).
It holds 50 + 2 per class level (max 250), refills 3 a second, and 10 a second standing still
(no D-pad: charging). The class techniques spend it; the game's spells keep the game's MP.

AP: four pips shared by the evades, one back every 1.5 s. The HUD shows the energy bar and the
pips under the class and weapon names.

| Technique | Energy | AP |
|---|---|---|
| Evade (any class's A dodge) | 0 | 1 |
| Blink (X then A), AI Blink | 10 | 2 / 1 |
| Kawarimi (hold A) | 15 | 1 |
| Shunshin, class L2+X move, class X move, clone jutsu | 15 | 0 |
| Issen, Hundred Fists | 20 | 0 |
| Iaijutsu, Telekinesis | 10 | 0 |
| Flying kick, grab and throw | 8 | 0 |
| Shield bash | 5 | 0 |

Releasing a bound familiar or mind (X again) is free.

### Weapon Flow's B chain (2026-11-20)

Like Ninjutsu's Taijutsu, Weapon Flow's B flows through weapons beyond its own three (sword
B, spear Y, bow X): each B press is the next of **sword, axe, whip, javelin**, then round
again; 1.5 s without B starts over. Selecting the class grants a missing chain weapon at
tier 0. (A held B keeps that press's weapon for charging.)

### Evades by class (2026-11-20)

- **Ukemi** (Martial Arts, Ninjutsu, Bushido) uses crouch, directional tumble and
  landing frames from pose `0x9E`. The generated frame pack uses entries
  `0xCC` through `0xCE` for the Boy, Girl and Sprite. The proxy fits the
  animation to the collision-checked dodge and mirrors the side roll for left.
- **Cartwheel** (Swordsmanship, Roguecraft). Forward cartwheels use drawn frames.
  Backward cartwheels can use the game's native pose on combat maps.
- **Up/down + A** (cartwheel classes, 2026-11-25): the game's own cartwheel facing the way
  pressed, front picture down and back picture up, its motion mirrored each frame (walls
  stop it). A alone is always the backward jump, facing kept. Off combat maps, where the game
  refuses its cartwheel, it is drawn. **Left/right** drawn cartwheels: mirrored to the left
  only (flipped from before, player). Verified on map 130: up -23 px, down +25 px.

### Monk and Weapon Flow B chains with power attacks (2026-11-20)

- **Martial Arts B:** punch, punch, then the glove's power attacks level 1, 2, 3, then the
  **spear's power attack as the finisher** (its highest learned level). The monk's glove has
  every power level (1-8) unlocked while held, whatever the game had learned.
- **Weapon Flow B:** sword, axe, whip, javelin, then the **spear's power attack** as the
  finisher. The spear (both classes' Y) never comes up between, only as the finisher.
- A power step is a tap: the proxy holds B until the level is charged, then releases it, and
  the game plays its own power attack. 1.5 s without B starts the chain over.

### Artificer portals (2026-11-20)

**Hold X** half a second: portal A where the Artificer stands (blue ring); hold X again
somewhere else on the same map: portal B (orange). Step into one and he comes out of the other
(in smoke, Blink's travel); he has to leave both before the next trip. A third hold starts a new
pair; changing maps closes them. 20 Steam per portal, travel free. **Tap X** is still the bomb
(now thrown on release).

### Casters talk and open chests (2026-11-20)

With an NPC or a chest right in front, a caster's B goes to the game (talk, open), no spell and
no swing; elsewhere B is the spell, never a punch.

### Companion classes and colours (2026-11-20)

The girl and the sprite have classes too. In the CLASSES menu (Start), **X** switches
whose class the list shows and sets: **BOY → GIRL → SPRITE** (the header says whose).
Up/Down and B work as for the boy; the `*` marks that member's class, B on it again
clears it. Defaults: the girl **Divine Arts** (Cleric, she heals), the sprite **Magic**
(Mage). Levels are the party's, shared with the boy.

- **Colours:** each companion's clothes take their class's colour ramp, the same ramps
  as the boy's (`src/som_class_palette.h`), on their own sprite palette (girl palette 2
  at 7E:DF40, sprite palette 3 at 7E:DF60). Hair and skin keep their colours: masks
  `SOM_GIRL_CLOTHES` / `SOM_SPRITE_CLOTHES` in `src/som_ai_proxy.c`, judged from the
  live palettes, not yet seen on screen (the saves tested had both companions dead).
  Unlike the boy, their hit and cast flashes are not held off.
- A companion not yet in the party (no HP) keeps that palette untouched.
- Saved in `som_ai_disciplines` as a trailing `p <girl> <sprite>`; older files load with
  the defaults.
- **Class weapons** (since the next change the same day): each companion is handed her
  class's B weapon (casters: bare hands) once per class choice and session; missing class
  weapons are granted at tier 0; the ring can change it afterwards. The weapon switch is
  the game's `$C0:EA41` for her actor: her effect-tile buffer 7E:2000 + who*0x1000, her
  `D030 + who*0x40`, and Turbo's per-hero plane converters (boy masked `$C1:C5D1`, girl
  xor `$C1:C731`, sprite inverted `$C1:C8A9`), checked byte for byte against all three
  live buffers. Her model must first rebuild what the game loaded for her, else refused.
  A weapon another member holds is shared; `$CC54+type` bits 6-7 keep one holder (1 boy,
  2 girl, 3 sprite).
- **Class moves** (`src/som_ally_moves.h`): the game's ally AI still walks and swings
  for them; the proxy adds the class technique when it fits, from the companion's own
  energy and AP (never the boy's bar), 4 s apart: Whirlwind, Hundred Fists, Shunshin,
  Arcane Bolt, Holy Light (heals the most hurt member under 60 %), Shadow Bolt, Spirit
  Lash, Healing Waltz, Flow Chain, Shadowstep, Issen, Volley, Grand Cross, Soul Drain,
  Bomb, Mind Blast. Damage goes through the monster's pending-damage byte, heals through
  the hero's queued heal +0x1F3; no kill credit for the boy's perks. Ranged and caster
  classes also take their class evade (1 AP) when a monster comes within 18 px; melee
  classes only under 40 % HP, since the game's AI must stand close to hit.
- Tool/AI command `ally_class_<1|2>_<class>` sets the girl's or sprite's class.

With the hybrid or memory player AI, companions favor the hero planner's target for
class techniques when the target is within reach. When the hero retreats, the proxy
pauses offensive companion techniques and allows class heals. The game still handles
companion movement, basic attacks, and Action Grid behavior. Joined companions with a
class are eligible while controlled by the game's AI. Orders expire if the bridge stops
refreshing them.

Set `SOM_AI_PARTY_AI=support` on the AI service to favor class healing throughout field
play, or `SOM_AI_PARTY_AI=independent` to keep autonomous companion techniques. The
default is `auto`. Direct bridge launches use `--party-ai auto|support|independent`.

### Party panels, Seiken Densetsu 3 style (2026-11-20)

Each member with a class gets a panel in their own HUD column, under their portrait
(found as HUD tile 0x1E8 + 2*who in palette who + 1):

```
[portrait] 164/175        <- the game's HP
           SAMURAI        <- class
KATANA                    <- boy: class weapon or spell set; companions: their class move
[==== energy ====] ....   <- class energy bar + 4 AP pips (no text: the column is 78 px)
```

The energy name (Ki, Chakra...) is no longer printed; it still appears in the "Not enough"
message. A member without a class shows no panel.

### Party Classes in the settings ring

From the field, press **R3** and use Up or Down to reach the ring with Equip
and Status. **Classes** uses the Action Grid icon in the native ring, between Window Edit
and Equip. Press Left from Window Edit or Right from Equip to highlight it, then
press **B**. The editor opens for the ring's owner. X switches party member,
B equips the highlighted class, Y starts reordering, and A returns to the ring.
L1 or R1 switches between Classes and Music. Controller Settings is available
in its usual slot. The editor uses the native Status background tiles and palette.
Press **Select** in Classes to open the highlighted class's guide. Up/Down browses
the classes, and A or Select goes back. L1/R1 opens the separate Music page. Weapon
percentages describe the current
style or class weapon, where 100% is the unscaled weapon value. Available move
previews use the existing hero animation pack.

Player 2 uses **Start** to join and to leave (the hero goes back to the computer);
Start and the stick clicks do not open a second ring menu. If Player 1 leaves or
their hero dies, Player 2's pad takes over every Player 1 control until Player 1
presses Start again. The first pad to press a button is Player 1, the next Player 2; a pad that drops
gets its place back. **Select+Start** held on either pad quits with an auto-save.
While the AI plays, **L2+Start** on either pad stops it. In field combat, Player 2 uses the same class
bindings as Player 1: **R2** guards, **R1** changes style, **L3+B** is the style
shortcut, **L2+R1/L1** changes the controlled member's class, **Y** uses the
class's second weapon or spell, and **X** casts or uses its class action. **A** evades
with the class's own dodge and **L2 + D-pad signs, then B/Y** charges a special
(one such move at a time between the two players). **B** runs the class's own string and
**L1** locks on; both players keep their own string and target. **L2+X**
requests the actor-specific class move. Party class setup remains in Player 1's
Classes editor.
**L2+X** requests the companion class technique for the Girl or Sprite.

## Enemy scaling

Regular enemies keep their native level or scale to two levels above the
highest-level party member, whichever is higher. Their HP, attack and defenses
also rise. A level increase preserves the enemy's remaining HP percentage.
Bosses, chests and familiar allies use their existing rules. Ranked enemies
apply their rank bonus to the scaled HP when they are promoted.

## Mixed-weapon B strings (2026-11-23)

Swordsmanship, Ninjutsu, Weapon Flow and Roguecraft have a B string like the monk's, played by
hand (the AI keeps its own chains). Tap B four times, B again for the heavy move, and B once more
for a finisher chosen by the held direction, counted against the nearest enemy. Two commands work
at any time: toward, toward + B and away, toward + B. A pause of 1.5 s starts over.

| Class | B x4 | Heavy | Finisher: none / toward / away / sideways | toward, toward | away, toward |
|---|---|---|---|---|---|
| Swordsmanship | sword | Axe Cleave | Saber Wave / Thrust / Step-Back Cut / Wide Sweep | Bash Cut | Parry Cut |
| Ninjutsu | fist, fist, sword, fist | Taijutsu | Shuriken / Dash Cut / Retreat Shuriken / Shadow Spin | Shunshin Cut | Kawarimi Fist |
| Weapon Flow | sword, axe, spear, whip | Javelin Step | Axe Breaker / Javelin Leap / Whip Guard / Spear Wheel | Spear Charge | Triple Shot |
| Roguecraft | sword | Quick Shot (bow) | Sleep Flower / Backstab / Cartwheel Shot / Side Stab | Slip Past | Smoke Hop |

Shunshin and Kawarimi still cost their Chakra; without it the cut comes alone. Sleep Flower costs
its MP; when it cannot be cast the rogue plays Cruel Cut. The Kusarigama style keeps its own B.

## Camera

The picture zooms and pans by itself (`src/som_camera.h`). Settled shots are
crisp: 1.5x in a 512x448 picture (dialogue and fights that fit) or 2x (the
lead after 10 s standing still with no monsters). If 1.5x slows the frames a
governor turns it off for the session: dialogue then uses 2x, fights 1x. The HUD and text windows are
the game's BG3 layer and keep their exact place and size. It changes only the
picture. Off switch: an empty file `cores/experimental/som_ai_camera_off`, or
the commands `camera_off` / `camera_on`.

Speech bubbles show only with **AI debug messages** on; plain boxes always do.

**Player 2 AI** (AI menu, ACTIONS page): the AI joins as player 2, follows and
fights. A press on pad 2 while you play takes over for good; the switch goes off.

## Classless games

A new game starts all three heroes without a class: plain weapons, block (R2),
evade (A) and target (L1). At level 5 a hero may choose the first-tier classes
the stats allow (main stat within 2 of the best, second within 6): Swordsmanship
STR/CON, Martial Arts STR/AGI, Roguecraft AGI/STR, Archery AGI, Magic INT,
Divine Arts WIS, Support & Dance WIS/AGI. Second tier at level 15, after the
class it grows from: Ninjutsu (Rogue), Bushido and Weapon Flow (Swordsmanship),
Dark Arts and Summoning (Magic), Vampire Hunting and Artifice (Archery),
Psionics (Divine Arts), Necromancy (Dark Arts). A passive works only for a
class that hero has or has had; the girl and the sprite have passives too. Saves made before this rule keep everything.

The ring's Classes menu has more pages on L1/R1: Music, then the AI menu
(ACTIONS, COMBAT, QUEST). Its last row starts the AI.

## Class attacks and Whacked

B is no jab for these casters. **Mentalist, Dream Petal:** a slow lotus; a
critical (12 % + level/5, at most 30 %) doubles it and puts an ordinary monster
to sleep. **Cleric, Healing Touch:** heals the most hurt ally in reach; burns
the undead; judges any other monster (heroes who hit it heal); else a small
self-heal. **Dancer, Ribbon:** light damage in front, slows monsters, readies
allies' weapon gauges. Bosses and familiars are never put to sleep or slowed. **Mage, Arcane
Orbs:** three homing orbs, one back every 54 frames, a weak spark without one.
**Warlock, Hex Chain:** binds the nearest monster and drinks a share of the
damage. **Summoner, Wisp:** flies out and back, may hit both ways.
**Necromancer, Bone Spikes:** run through every monster in line. Monk (+130 %),
Archer (+35 %) and Artificer (+60 %) deal a share more on weapon hits; any
hero's attack value is 86 % as Weapon Flow and 130 % as Bushido.

**Vampire Hunting string:** whip from afar, silver sword up close, axe to end
it; mid-string the whip takes over when nobody is near. Thrown weapon looks:
the hunter's is a painted holy cross (+50 % on undead), the ninja's the ring
menu's shuriken icon. **Execution:** a finisher kills a monster (no boss) left
under a fifth of its HP. The Classes menu shows the marked class's weapon icons.
A weapon mask frame without art shows the game's own for that frame only.

**Whacked:** a critical hit (the game's "gets whacked!") that kills an enemy
makes the camera punch in on it; AI menu, *Whacked camera*, turns it off.

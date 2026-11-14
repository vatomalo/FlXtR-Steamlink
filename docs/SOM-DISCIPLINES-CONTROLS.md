# SoM enhanced combat: disciplines, controls, and evade discovery

Development branch: `feature/som-disciplines-controls-20261114`, based on the **working** familiar branch `feature/som-familiars-working-20261114` (commit `595d57e`). Do not merge older `clone_familiar_visual` experiments over `som_familiar.h`.

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

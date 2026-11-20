# Secret of Mana: summoned sprite, Ukemi and Psionics test notes (2026-11-20)

This branch intentionally does not modify the ROM or merge into `main`.
The fixes operate inside the experimental Libretro input proxy.

## Familiar / summon sprite graphics

**Failure mechanism:** `som_clone_enemy` previously cloned the monster's WRAM
object and sprite buffer into a new slot, then the familiar's VRAM capture read
that new slot's still-stale VRAM instead of the **source** monster's actual
on-screen tiles. Crossing to a different map then reapplied *absolute source
tile IDs and palette addresses*, potentially overwriting unrelated actors'
sprites and giving the familiar scrambled graphics.

**Fix:** remember the original live monster slot; capture its actual decoded
VRAM tiles and colours; retain the captured source tile base. On later maps,
use the destination's initialized tile bank and OBJ palette, translate source
tile indices relative to the destination, and never reapply to the original
map's absolute tile IDs. If the destination's VRAM tile bank is not initialized,
its palette is not a monster palette, the palette is already used by another
actor, or mapped tiles overflow OBJ VRAM, restoration waits for another safe
slot instead of corrupting sprites. Captured source artwork is not overwritten
by the relocated object's periodic saves.

**Limitation:** old `som_summon.bin` boss packages contain only the native
object, its 4096-byte WRAM graphic buffer, and 32-byte palette, **not** a VRAM
capture. They continue to use the native-slot graphics route; their animation
across different maps still requires live visual verification and may need a
versioned summon package with the captured sprite atlas. Current tests cover
address safety and static tile remapping, not all animation frames or hardware
DMA timing.

## Ukemi for Samurai and Ninja

When `som_cartwheel_frames.bin` is installed, the proxy uses its original-ROM
standing-sprite quarter-turn frames (kind `0x80` to `0x82`) for Ninja and
Samurai Ukemi rather than relying on the native miss-reaction pose `0x93`.
The hero's native sprite is hidden only while the frames are drawn, then
restored. Actual movement remains collision-checked; there is no forced pose
or teleport through walls. If the frame pack is missing, the old native reaction
and carried-hop fallback remain.

The pack is built by `tools/som_cartwheel.py` from the user's own ROM and
must be installed at:
`/home/apps/retroarch/cores/experimental/som_cartwheel_frames.bin`.

A device test is still needed to confirm the full animation, palette and
visibility restoration on the Steam Link, including map transitions.

## Psionics Telekinesis

**L2 + X** (controller), or the AI command `telekinesis`, pulls the nearest
native treasure chest (monster-slot identity `0xEA`) from up to 160 pixels
toward the Mentalist, stopping 28 pixels from the hero. Chest identity, drop
contents and interaction state are never rewritten. It moves at most three
pixels per frame, only over known walkable ground on the same floor, for
up to 90 frames; cooldown is 180 frames. A map transition, reused slot,
invalid floor, wall or non-field game state cancels the movement. Bosses,
monsters, NPCs, scripted objects and quest objects are **not** valid targets.

Other movable props can be added later with verified native identities and
interactions; they should not be guessed from arbitrary object slots.

## Class balance

The AI's situation-scoring now recognizes Hunter's Grand Cross, Artificer's
crowd-control explosives, Mentalist's mind control and Necromancer's
MP-dependent Energy Absorb. It preserves the intentional design that Ninja
uses its own Taijutsu instead of switching to Monk for fists.

This is **class-selection balance**, not a full numerical DPS rebalance.
Monster/skill damage balance needs live fight measurements before changing
damage coefficients, especially as disciplines progress toward level 100.

## Automated checks

The SoM workflow on this branch runs the existing C/Python tests, diagnostic
Linux proxy build, and two additional pure-C tests:
`tests/som_telekinesis_test.c` and
`tests/som_familiar_gfx_test.c`.

The tests verify chest-only selection, floor/slot/map guards, simulated
walking movement and target-relative VRAM/palette restoration without touching
old source art.

## Live acceptance checklist

1. Summon a normal monster in its home map; inspect limbs, palette, idle and
   attack frames, then carry it across at least two different map tilesets.
   Confirm existing enemies and NPCs are never recoloured or garbled.
2. Load a legacy boss summon package, fight, leave and re-enter a map. If this
   remains garbled, capture the original boss VRAM atlas and version its
   package rather than hardcoding its tile addresses.
3. Try Ninja and Samurai Ukemi up, down, left and right, near walls and after
   camera scrolling. Check animation frames, no double body, and visibility
   restored after menu entry and map transitions.
4. Spawn an actual dropped treasure chest (`0xEA`), stand 40–160 pixels
   away on open terrain, press L2+X, and then open the pulled chest normally.
   Test another floor, a wall, a boss, and a room transition; none may move.
5. Run ordinary combat and save/load to ensure no progression, inventory,
   familiar control or controller regressions.

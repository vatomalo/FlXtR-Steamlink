# Visible armour (Phase C.2)

`src/som_armour.h` and `som_bushido_armour()` in `src/som_ai_proxy.c`. Status
2026-11-26: the Samurai torso layer works on the boy in real-ROM headless renders
(proxy + Turbo ROM, map 52, standing, walking, facing down, swings). The girl and
the sprite were checked on composed sprites only. Not played live.

## How the mask is found (measured on the ROM)

A hero is drawn from 2-4 OAM pieces of 16x16. The game uploads their tiles per
animation frame: boy and girl in rows 0x1C0-0x1CF, sprite in 0x1E0-0x1E7 (0x1E8+
are the HUD portraits). The renderer composes the hero's own pieces from the OAM
shadow and VRAM. The torso is palette index AND body band, found per frame:

| hero | torso indices | neck | rows |
|---|---|---|---|
| boy | 8, 9 (shirt, also the face) and 10 (stripes) | below the last hair row (2-4) of the top 16, at least 14 rows from the top | 8 |
| girl | 10, 11, 12 (dress) | below the last hair row of the top 18, at least 14 | 6 |
| sprite | 10, 11, 12 (tunic, also in the cap) | first tunic row 9 or more rows down | 8 |

So the face, hair, arms, hands, legs and the weapon (separate tiles) are never
painted, in any pose the game draws. Rotated poses (somersaults, lying down) keep
the same rule and can look off for those few frames.

On screen each index's colour is learned from the hero's own pixels in that frame
(the most common framebuffer colour per index). Turbo's colour filter and the
class palette tint sit between the WRAM palette and the screen. A torso pixel
showing anything else is covered (scenery, another sprite, the katana) and is
left alone.

Two bugs of the first version (8e3e958) are fixed: the OAM y was used +1, so
almost no pixel matched (measured 2-8 of 25-58 painted; now 31 of 31 standing),
and the sprite's tiles (0x1E0) were never accepted.

## Outfits

`SomOutfit` holds colours per body slot (`SOM_SLOT_TORSO` only so far),
independent of class and stats. Samurai: crimson lacquer in three shades by the
native index's rank, golden lacing every third row from the neck. Other outfits
(Ninja, Monk, Vampire Hunter and the rest of the table in the request) and other
slots (helmet, shoulders, gloves, legs) are not drawn yet. Palette replacement
cannot add silhouettes; those need generated art (som_art_gen.h) and per-pose
attachment points.

## Kabuto and shoulder plates (2026-11-26)

The player found the crimson torso patchy ("red shirt patches ... might look better with a
kabuto"). Now:

- **Sode:** the boy's arm indices 5-7 in the first 4 rows of the torso band are painted too,
  in alternating crimson bands, so the cuirass reads as one piece.
- **Kabuto:** drawn art (`som_kabuto_art`, 16x11: front with gold kuwagata horns, back
  with a laced shikoro, side with the crest forward; left = side mirrored), placed per frame
  on the top rows of the head the game draws (`som_kabuto_place`), by the hero's facing
  `+0x10`. Real-ROM shots on map 130, four facings.

## Cost and tests

Compose plus mask: about 5.6 µs per armoured hero per frame on x86
(`make test-som-armour`, synthetic figure, no ROM art), only for heroes whose
class is Samurai. Not measured on the Steam Link.

Real-ROM check: `tools/som_headless/run.py` with `cmd class_10` on a field state,
then shots. The colour pass draws before the camera scale, so zoom follows.

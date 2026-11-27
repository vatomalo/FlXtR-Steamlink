# SoM modular appearance integration

| System | Existing integration |
| --- | --- |
| Sprite identity and animation | The proxy tracks party actor records at WRAM E000/E200/E400. Facing at 0010 and pose at 0026 are actor-local. The same pose value is not presumed to be equivalent across actors. |
| Native sprite rendering | Snes9x 2005 draws OBJs in `source/gfx.c:DrawOBJS`. Its tile drawing applies native clipping, flips, sprite priority, palette and colour math. The proxy also snapshots the game's OAM shadow at WRAM 0800 before running the core. |
| VRAM and palette | The backend exports VRAM through libretro. The proxy combines 4bpp tiles from VRAM with OAM in `som_armour.h`, and uses WRAM palette shadows for class palettes. The native renderer uses CGRAM-derived screen colours. |
| Existing art | `som_armour.h` and `som_bushido_armour` draw armour and headgear. `som_weapon_mask.h`, weapon art and throw skins implement weapon visuals independently. Keep those attachments outside the atlas. |
| Extended animation | Combo, guard and parry use actor timing and effect state in the proxy. Evades can use native poses, while Ukemi and flykick draw separate frame-pack art. Clones, trails, teleport and cinematic effects are generated separately. These paths need explicit mappings or native fallback. |
| Menus and persistence | The ACT pages, retained navigation and input gating live in `som_ai_proxy.c` and the menu headers. Save-state progress uses the versioned trailer after the backend serialization payload. Appearance data can use a separate versioned record without changing the existing trailer. Native naming transitions and their WRAM protocol still require emulator verification. |
| Camera | The director zooms the rendered world in `som_camera.h` after world effects and before UI overlays. Renderer-level replacement enters the same camera path as native sprites. |
| Builds and tests | `scripts/build-som-ai-proxy.sh`, `scripts/build-cores.sh`, Android and desktop build scripts, and Windows and Android workflows are the distribution entry points. `tools/som_headless/run.py` and its C harness drive actual ROM scenarios. Make targets cover proxy, camera, priority, combat and armour. |

The previous Monk head mask inferred a scalp outline from changing sprite colours, then filled removed pixels with neighbouring scenery. The facing byte can lag the actual drawn pose during turns. Head silhouette, visible palette shades and anchor therefore changed across frames, and the fill could copy neighbouring sprites or scenery. These issues are documented in `docs/SOM-ARMOUR.md`; a complete frame-by-frame reproduction of every reported failure is unavailable.

The first renderer experiment supplies already-compiled IDX4 tile pixels only inside native OBJ draws. VRAM injection would share tile storage between actors and interact with DMA and invalidation. Final-frame composition would need to reconstruct pixels behind the original sprite. Selecting a decoded tile at draw time leaves native transparency, clipping and priority in charge. Unsupported poses and unmatched OBJ tiles use the native path. This first slice is limited to native OBJ footprints and native palettes. Whole-frame atlas compilation needs explicit per-pose tile layouts, anchor and socket metadata supplied by the pack author.

## Prototype verification

Build the pinned backend with the repository's Snes9x patches. Generate
test packs with `python3 tools/som_headless/make_sprite_atlas_fixture.py DIR`,
export its three printed assignments and point `SOM_HEADLESS_BASE_CORE` at
the patched backend. Run `tools/som_headless/run.py` with your own Turbo ROM,
field-capable SRAM and `scenarios/sprite_atlas.txt`. The diagnostic sources
are original mask and crest patterns, composed on a named head socket.

The Linux headless run reached the native field, rendered replacements in
each of the three party slots, and captured idle and four directional
movement frames. The native and atlas runs had byte-identical WRAM dumps
at all five capture points. The 1200-frame host samples were 0.867 ms/frame
with native rendering and 0.836–0.954 ms/frame in two atlas runs. That range
does not establish a performance improvement. This is headless Linux
throughput, with no Steam Link display, and the packs cover only five
observed pose keys. The three diagnostic banks total 13,624 bytes on disk.

Outstanding work includes complete original or user-supplied frame packs,
verified animation timing, exact visual alignment, occlusion and dynamic
attachment checks in additional gameplay scenarios, and appearance profiles
that can survive save, load and restart. The developer selector loads an
already-compiled bank at content load. There is no ACT or naming integration,
confirmation-time compiler, or complete hybrid character atlas yet.

## Naming-page entry

The creator opens once when the Turbo native naming page is detected.
The native constructor sets WRAM A20F and A156 to 3, and the native
confirmation routine reads the target party slot at A22B. A clean
headless New Game run verified those fields on the blank and populated
name page. Press B or Start to accept the appearance, A to cancel, or
X on the keyboard to revisit appearance. Left and right switch between
native and a supplied compiled atlas for the active party slot.
Set `FLXTR_SOM_CREATOR_OFF=1` to skip the appearance panel entirely.

The display uses a two-panel layout with an enlarged native preview and
appearance selection. The atlas option displays an indexed tile sample.
Input remains captured until buttons are released when leaving the
appearance panel. The original keyboard owns glyph insertion, name
validation, confirming the name, initializing the hero and resuming
the opening event. The proxy does not write those name or event fields.

Appearance selection in this prototype is session-only. It switches
between native rendering and a preloaded atlas; confirmation-time
compilation, an animated custom preview and save persistence are separate
work. The emulator scenario checks cancel and reopen, a held confirm
without a leaked glyph, and a native name accepted into the opening scene.

## Original starter bodies and skin

The naming preview defaults to starter art when the backend exposes both
OBJ atlas and palette callbacks. Left/right selects native or starter.
Up/down selects Ivory, Golden, Copper or Umber. All three slot designs
and both preview poses are original indexed art. The generator compiles
8×8 draw tiles from the same frames the preview uses. The renderer applies
the selected palette only to replacement tiles and caches the game's
brightness conversion. It does not rewrite CGRAM or game memory.

Starter rendering is mapped to a limited set of native idle and walk poses.
Unmapped poses and pieces use native graphics. Selection remains
session-only and the preview is a two-pose loop. Environment variables
`FLXTR_SOM_STARTER_P1`, `FLXTR_SOM_STARTER_P2`, and `FLXTR_SOM_STARTER_P3`
can set a development fixture's skin (0–3) before loading a field save.

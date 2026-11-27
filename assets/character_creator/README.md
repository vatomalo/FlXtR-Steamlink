# Character sprite packs

`tools/som_compile_atlas.py MANIFEST --out CACHE` compiles a version-1 JSON
manifest to a content-addressed `.sat` file. Install Pillow for PNG sources.
Binary PPM is also accepted for dependency-free diagnostic fixtures.

The target `archetype` is `randi`, `primm` or `popoi`. Set `asset_id`,
`version: 1` and optional `chroma_key: "ff00ff"` or `"00ff00"`. Each named
`sources` entry specifies a source `path`, a map of RGB hex strings to OBJ
indices 1–15, and optionally a list of `compatible` archetypes. Index zero
is transparency. Palette mapping is independent of alpha and of runtime
native sprite replacement.

A frame specifies canonical `action`, `facing`, `duration` in native ticks,
native `pose`, optional named `sockets` with integer XY pairs, and `tiles`.
Each tile has its native `tile` index, optional `origin` in frame coordinates
and back-to-front `layers`. Each layer has a named `source`, a `rect` of
[x,y,width,height], optional pixel `offset` and optional named `socket`.
A top-level `required` list such as `["idle/down","walk/left"]` enforces
coverage beyond the frames supplied. Durations and canonical names are
authoring metadata. Native timing drives the renderer.

Use the optional development variables `FLXTR_SOM_ATLAS_P1`,
`FLXTR_SOM_ATLAS_P2`, and `FLXTR_SOM_ATLAS_P3` with matching compiled file
paths. The pinned Snes9x backend needs the sprite-atlas patch. Missing
banks and unmapped tiles use the native art. The runtime still uses the
native OBJ palette. Sources and compiled packs should be distributed only
with appropriate rights.

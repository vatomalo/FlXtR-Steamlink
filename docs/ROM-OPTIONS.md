# ROM options and RetroArch scaling

Selecting a ROM opens Play, Core, Scaling and Output. Core lists installed
libraries plus Automatic. A saves the choice for this ROM; B returns.
Left/Right on the core row cycles choices. Start launches the game.
The Y core-picker shortcut remains available. Core choices are keyed by ROM
path identity in rom-cores.tsv, so title prettification or changed import IDs
do not lose the selection. Existing game-cores.tsv choices remain a fallback.

Scaling and Output are shared game preferences, also in Settings > ROMs.
They do not change the movie player or audio/video timing.

- Fullscreen Fit uses the core-provided aspect ratio.
- Stretch disables aspect correction and fills the output.
- 1:1 Core Pixels centres the core's native base geometry without integer zoom.
- Native output explicitly clears saved fullscreen dimensions and uses the
  current display mode. Other choices remain 240p/480p/720p/1080p.

Generated launch config clears stale custom viewports, integer scaling conflicts
and overscan crop. Core/content overrides are disabled for this managed launch
so they cannot replace the selected display settings. Global RetroArch config
and core-option files are not overwritten.

For 1:1, an isolated catalog subprocess loads the selected core/ROM, reads
libretro geometry and exits without running emulation frames. It uses a separate
probe-save directory. The parent stops a probe after eight seconds. Native
display dimensions are measured afresh. A framebuffer larger than the output,
a failed probe or a core requiring a hardware-rendering frontend reports an
error asking for Fit. The app does not silently crop or call integer zoom 1:1.

Geometry is the base geometry at initial load. Games that later change resolution
may need Fit. ZIP, BIOS and core initialization requirements can also prevent
the probe; game-probe.log records errors.

RetroArch's upstream enum keeps Core Provided=22 and Custom=23 in versions
1.7.4 through 1.21:
https://github.com/libretro/RetroArch/blob/v1.7.4/gfx/video_defines.h .
The vendored libretro API header retains its upstream license, with the notice
included in packaged builds.

First hardware test: choose Native output and Fullscreen Fit, then test Stretch
and 1:1. Low-resolution requests may be ignored or upscaled by the Steam Link
backend/TV; selecting a 240p render canvas does not guarantee 240p HDMI output.

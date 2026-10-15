# Series playback controls

Press X during an episode to play the next one, with or without the video menu
open. On the current DS4 mapping this is Square (the SDL X button). Keyboard:
F2. The video menu also lists Next Episode alongside the subtitle options.
Movies ignore the shortcut.

Settings → Autoplay Series enables continuous episode playback and is saved
across restarts. Default: Off. Flixer series continue across numbered seasons,
skipping specials, missing season numbers and episodes with future/unknown air
dates. Playback stops when no more aired episodes are available. Anime follows
its current ordered episode list, including page boundaries. Source failures
use the existing server fallback; stopping or an error does not skip episodes.
TV mode retains its separate channel scheduling behavior.

Tests cover season transitions, previous episode transitions, future episode
filtering, series end, saved on/off settings and the playback shortcut. Native
and Steam Link builds are checked; hardware playback remains to be tested.

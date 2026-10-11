# Steam Link core performance and launch recovery

Based on feature/coverart-neogeo-20261011 at dabe0df, including its folder defaults,
core picker, artwork, TV and subtitle changes. Pending older folder/core work is
saved separately; it was not overlaid on this newer implementation. Explicit
--library games/archive startup now also uses the folder screens (21/30), including
the return from RetroArch; upstream only changed the library-switch navigation.

## Hang evidence and limitations

The device game log repeats `MV_PE_InputBufWrite() failed: 0x80007061`.
Both libSLAudio and libSLVideo contain that diagnostic; the message alone does not
identify a video-decoder fault. Isolated SNES runs with normal drivers, null audio,
and null video all completed. The intermittent cause is not conclusively isolated.

The shell previously sent SIGKILL to every child immediately on exit. The player
handles SIGTERM and releases SLVideo and SDL audio resources on its normal exit.
Shutdown now gives children a shared three-second cleanup deadline, then forces
only remaining children to exit. A wedged network worker cannot block indefinitely.
The launcher detects actively growing repeated media errors even with interleaved
messages, keeps game-failure.log, and recovers the game process. This is recovery,
not proof that every proprietary driver hang is fixed. Normal gameplay has no timeout.
Verbose game logs now identify the exact ROM/core and initialization stage.

## Builds and measured results

Run `bash scripts/build-cores.sh` in the Steam Link SDK container. Sources are pinned,
built with Cortex-A9/ARMv7/NEON hard-float flags and -O3, and stripped. No fast-math,
emulated CPU underclock, forced frameskip, reduced resolution, or device overclock.
Publish dist/core-sources archives alongside binary packages and their licenses.

900-frame tests on the actual box (wall time includes startup, not gameplay FPS):

| Test | Installed | Tuned | Notes |
| --- | ---: | ---: | --- |
| MAME2003 Plus, Ghosts n Goblins, null audio/video | 6.648 s | 5.760 s | Same source revision 0353570; 13.4% less elapsed time |
| MAME2003 Plus, same game, normal audio/video | 17.531 s | 17.610 s | No demonstrated end-to-end gain at display limit |
| SNES9x2005, Seiken Densetsu 3, null audio/video | 6.128 s | 5.844 s | Earlier test; 4.6% less elapsed time |
| SNES9x2005 Plus vs tuned standard, null audio/video | 7.199 s | 5.844 s | Different audio emulation; not a like-for-like compiler gain |

Latest MAME e05caba tested slower (7.339 s vs 6.499 s installed); rejected.
Threaded video gave no meaningful benefit and is not forced. Brief startup/attract
benchmarks do not establish full-speed gameplay for all titles or demanding scenes.

## Selection and rollback

App-owned cores live in greenlink/cores with distinct `_flxtr` names. Existing
RetroArch cores, Neo Geo configuration, BIOS, ROMs and saves remain untouched.
Automatic defaults use the tuned build only for exact snes9x2005/mame2003_plus
matches. Per-ROM choices always win, including the original system core. Plus,
2002 and other explicitly configured SNES variants stay selected. Triangle exposes
the extra builds. FLXTR_USE_SYSTEM_CORES=1 disables automatic substitution.
For SNES/MAME launches only, disable shaders, rewind, run-ahead, frame delay and
hard sync. Audio quality and sync settings stay unchanged.

## Validation

`make test-games`: graceful cleanup, forced-exit deadline, exact launch arguments,
bundled/default/manual/fallback selection, untouched Neo Geo settings, mixed-error
watchdog recovery and healthy game completion. Native shell/catalog build passes.
`python3 tests/update_test.py` passes. The existing full `make test` stops in an
outdated playback-menu test expecting immediate seeks; current UI uses seek preview.
The old shell test also expects a library view shortcut changed upstream. These
unrelated fixture mismatches are not reported as passing.

Packaged cores each completed two consecutive 300-frame launches with normal audio/video
on the box, with no media-input errors. Test ROM copies isolated SRAM writes.

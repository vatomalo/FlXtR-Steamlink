# RetroArch SDL runtime investigation

The cross-core freeze was reported in Castlevania II (FCEUmm) and Magic Sword
(FBA2012). The inspected box had package 2e5b4bc, built on the feature branch
without the previously published 8609e21 cleanup changes. Those changes are now
merged with the newer feature work rather than discarded.

The installed 2019 RetroArch loads firmware SDL2 compatibility (2.3200.69), which
in turn loads SDL3. The SDK instead carries native SDL2 2.31.0-p8771279. A private
copy lets us test the compatibility boundary without replacing system libraries
or rebuilding/changing cores. This is a candidate workaround, not yet proof of
the root cause. A clean title-screen run is not sufficient validation of the
reported walking-in-town failure.

Builds pin the native binary to Valve SDK commit
62b4d098d1472c3534dd098ca2a0e0e10712f1c6 and verify SHA-256
c9d19c7ae28243b3739e2f14a4f19cdd48646bed063b26607d25b19dbc148932.
SDL attribution/license is included in the package. Only games_run, immediately
before exec of RetroArch, prepends app/retroarch-libs to LD_LIBRARY_PATH. The
shell, player, ScummVM and system library paths are not changed. Existing path
entries are retained. FLXTR_USE_SYSTEM_SDL=1 opts out for comparisons/rollback.

Each game launch retains two previous logs, and nonzero exits retain
 game-failure.log, so launching another title does not immediately erase evidence.

Validation: focused importer/runtime tests cover private SDL selection, preserved
search paths, opt-out and unchanged parent environment. The device can initialize
native SDL2 with its DS4 and sound, with no SDL3 in its loaded-library map.
Walking/reproduction results must be confirmed before calling this fixed.

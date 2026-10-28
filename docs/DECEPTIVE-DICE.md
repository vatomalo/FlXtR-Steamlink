# Deceptive Dice native Steam Link preview

Games → DECEPTIVE DICE → A installs and launches the game directly on the
Steam Link hardware. The first launch downloads a fixed release from the
owner's GitHub repository over verified HTTPS and validates its pinned SHA-256
before extracting. Later launches work offline. During the initial download,
details are written to `game.log`; the TV may remain blank until it completes.
Back + Start exits the game and returns to FlXtR.

The downloadable preview update is an overlay for an existing FlXtR Steam Link
installation. Extract it into `/home/apps/` while FlXtR is closed. Back up
`greenlink`, `greenlink-catalog`, and `greenlink.sh` first. It adds the launcher
and release configuration and replaces the shell/catalog binaries; the existing
player, assets and configuration remain in use. Disable auto-update while
testing this preview, since mainline releases do not yet include the entry.

To remove the downloaded game, close FlXtR and remove only its
`native-games/deceptive-dice` directory. The game archive can also be extracted
into `/home/apps/` and started with its `launch.sh`.

The 0.2.1 preview preserves the original 640�360 layout and scales the completed
frame using the GPU. Stamina state and core combat formulas now run in C++;
battle sequencing and presentation still contain JavaScript.

The previous GPU candidate measured 35.65 FPS in a seeded combat test on the
physical Steam Link, compared with 2.95 FPS for the initial software renderer.
The C++ build passes standalone core tests and 3,040 comparisons against the
original JavaScript methods. The C++ build also passed the same checks on physical Steam Link hardware
and measured 35.22 FPS across 360 frames after warmup. The 0.2.1 audio fix packages Ogg assets for the SDK mixer and fixes first-request
music startup. Effects are confirmed audible; native music playback passes.
The seeded test with music/effects enabled measured 32.86 FPS.

Existing installations keep their downloaded game for offline launches. To
fetch the new pinned version, close FlXtR, move the existing game directory
to a backup, apply this overlay, and launch DECEPTIVE DICE again.

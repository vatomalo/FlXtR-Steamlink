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

Validation covers ARM compilation, emulated game execution, input/combat smoke
tests, downloader hash and archive rejection, offline relaunch and catalog
handoff. Physical Steam Link graphics, audio and frame rate remain unverified.

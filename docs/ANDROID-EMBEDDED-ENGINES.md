# Embedded Android engines

FlXtR hosts PPSSPP, Dolphin and ScummVM through Libretro; no external emulator
app is launched. Both Android ARM architectures are packaged. Android 8 remains
the minimum, but performance and graphics support depend on the device.

## Import and start

- PSP: add a CSO or PBP, or choose PPSSPP in a game's CORE menu for an ISO.
- GameCube/Wii: add a GCM, RVZ, WBFS or DOL; for ISO files choose Dolphin in CORE.
- ScummVM: Games > OPTIONS > ADD SCUMMVM GAME FOLDER imports a complete game-data
  folder. ScummVM identifies the game from that folder. The existing `.scummvm`
  hook-file workflow also works when its parent folder is granted.
- Games > OPTIONS > SCUMMVM LAUNCHER opens the embedded ScummVM launcher.

ISO and PBP formats overlap across systems; selecting a core is explicit and
saved per game. The frontend does not relabel every ISO as a PSP or Wii game.

The first launch downloads the engine's official system assets, verifies their
SHA-256, then extracts them within the app's private system directory. Asset
installation is bounded and cancellable. Changed upstream archives are rejected.
Subsequent launches use the installed assets offline. Game data is not downloaded.

Touch controls remain hidden by default and can be enabled from the game menu.
Gamepads provide both analog sticks; ScummVM also receives pointer/mouse and
physical-keyboard input. Individual core controller features may need further
device-specific validation.

Engine-managed saves live below `files/games/core-saves/<core>/`. In-game save
files and emulator save states are different. ScummVM uses its own in-game save
menu and does not support frontend save states. Cloud synchronization is not
implemented by this change.

## Validation

On a Samsung A52 (Android 12):

- All three native cores loaded and reported their versions.
- A synthetic GLES core passed pixel readback, audio, analog-input and state tests.
- PPSSPP presented 127 frames from FlXtR's own PSP homebrew build and restored
  its 75,497,472-byte save state.
- Dolphin presented 120 frames and 5,128 audio samples from the public GameCube
  240p Test Suite in an off-screen test; an earlier foreground run presented 122
  frames. Later foreground reruns were interrupted and are not counted as passes.
- ScummVM's embedded launcher presented 30 frames through the filesystem backend.
- Beneath a Steel Sky produced 137 frames and 2,804 audio samples.
- Folder imports, nested data and same-size changed-file refresh passed.
- NES, SNES and GBA passed video, audio and save-state regression checks.

These are smoke tests, not a claim that every game, controller or Android model
works. General game compatibility, extended gameplay, Wii-specific input and
ScummVM in-game save behavior require additional validation.

## Updating from older previews

This universal APK includes large native engines. Older previews cap update
downloads at 128 MB, so install this APK directly once, keeping the existing app
installed to preserve its data. This version accepts verified updates up to
256 MB. Android still asks you to approve installation.

Pinned core binaries, runtime assets and corresponding source snapshots are
published at https://github.com/vatomalo/FlXtR-Steamlink/releases/tag/android-engines-2026.11.04.
The ARMv7 Dolphin build uses an older upstream revision than ARM64; packaging
both architectures does not guarantee the same performance or compatibility.

Core documentation:
https://docs.libretro.com/library/ppsspp/
https://docs.libretro.com/library/dolphin/
https://docs.libretro.com/library/scummvm/

Homebrew test source: https://github.com/ArtemioUrbina/240pTestSuite

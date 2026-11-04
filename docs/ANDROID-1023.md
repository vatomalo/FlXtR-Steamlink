# Android 0.10.23: embedded game engines

PPSSPP, Dolphin and ScummVM now run inside FlXtR, alongside the existing consoles.
The native frontend supports OpenGL ES rendering, both analog sticks, pointer
input and ScummVM keyboard input. Touch controls remain optional in the game menu.

Use the game's CORE menu to choose PPSSPP or Dolphin for ambiguous ISO files.
For ScummVM, use Games > OPTIONS > ADD SCUMMVM GAME FOLDER and select the entire
game-data folder. An embedded ScummVM launcher is also available there.

Phone posters are smaller in portrait and landscape; tablet sizing is preserved.
Game and settings data are preserved when installing over the existing app.

**Install this APK directly once if upgrading from an older preview.** The new
engines exceed the old updater's 128 MB limit. This release raises that limit to
256 MB for future updates. Android still requires installation approval.

Tested on Samsung A52, Android 12: PSP homebrew rendering and save-state restore,
GameCube 240p Test Suite rendering, ScummVM launcher and Beneath a Steel Sky video
and audio, folder import/cache refresh, and NES/SNES/GBA regressions. Synthetic
GPU tests also checked actual pixels, analog input, audio and state restoration.
These checks do not establish compatibility or speed for every game or device.

Includes ARM64 and ARMv7 binaries. ARMv7 Dolphin uses an older upstream build.
First launch downloads verified engine runtime assets; later launches work
offline. Games are not included. ScummVM uses its in-game saves. Cloud save
synchronization is not part of this release.

[Engine details and import instructions](../docs/ANDROID-EMBEDDED-ENGINES.md)

Pinned binaries, runtime assets and corresponding engine sources:
https://github.com/vatomalo/FlXtR-Steamlink/releases/tag/android-engines-2026.11.04

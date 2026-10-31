# Android console games

Open **More → Games** or **Library → Libretro games**. **Moonlight** is available
from the same menus and the Games screen; it uses the embedded Moonlight client,
including PC discovery, pairing, controller input and stream settings.

Add a game folder once through Android's file picker. FlXtR saves read access and
recursively scans it when the game library opens (at most once per 30 seconds).
**Scan now** refreshes it immediately. There is no periodic background service.
The default folder hint is shared storage `FlXtR/Games`. Use **Folders** to stop
scanning a folder. **Add files** also supports individual cartridge files and
single-file discs.

| System | Core | Files |
| --- | --- | --- |
| Game Boy / Game Boy Color | Gambatte | `.gb`, `.gbc` |
| NES | FCEUmm | `.nes`, `.unf`, `.unif` |
| SNES | Snes9x | `.sfc`, `.smc` |
| Game Boy Advance | mGBA | `.gba` |
| PlayStation | PCSX-ReARMed | `.cue` with its tracks, `.chd`, `.pbp`, `.iso`, `.m3u` |

Extract cartridge ZIP files first. Standalone `.bin` tracks are not shown as
games. CUE and M3U files require folder access so every referenced track/disc can
be read. References must stay inside the selected folder. Match filename case.
PlayStation M3U playlists support **Game menu → Next disc**.

Only the selected game's data is copied to private storage for the native core;
later plays reuse files when the provider reports the same size and modification
time. A changed file is copied again. Original files are never modified. Files
are limited to 2 GB each. A missing/unavailable source produces an error without
deleting existing library entries or save data.

**PS BIOS** imports an existing 512 KB `scph1001.bin`, `scph5500.bin`,
`scph5501.bin`, `scph5502.bin`, `scph7001.bin` or `psxonpsp660.bin`. BIOS files
and games are not downloaded or bundled. Save RAM, automatic resume and manual
states are stored per game. Existing Gambatte saves retain their filenames.

The FlXtR HOME activity and **More → Android Settings** remain available.
Android's system power-management configuration is not changed by this feature.

## Build

Run `scripts/build-android.sh` with the existing Android SDK/NDK requirements.
`prepare-console-cores.py` verifies `android/console-cores.lock.json` and caches
the official Libretro archives under `android/build/console-core-cache`.
Because upstream `latest` URLs change, retain that cache with a release; a hash
mismatch fails the build rather than accepting a different binary.

For incremental Java builds, `-PflxtrNativeDir=/absolute/path/to/jniLibs` uses
previously built native libraries instead of rerunning CMake/ndk-build. That
directory must include the matching JNI frontend, resolver, Moonlight, runtime,
and console cores for each requested ABI. Build tools 35.0.0 are required.

Core sources and licenses:

- FCEUmm: https://github.com/libretro/libretro-fceumm (reported revision `ac0d00a`)
- Snes9x: https://github.com/libretro/snes9x (reported revision `fae2fea`)
- mGBA: https://github.com/libretro/mgba (reported revision `7a12d6d`)
- PCSX-ReARMed: https://github.com/libretro/pcsx_rearmed (reported revision `7b0e8f8`)

License texts are included in `android/app/src/main/assets/licenses/`.

## Validation

The Android unit suite includes format detection, safe disc references, M3U
handling and BIOS filename validation. Debug instrumentation includes:

- `ConsoleSmokeInstrumentation`: load all additional cores; recursively scan a
  synthetic document provider; deduplicate; discover additions; replace changed
  imports; stage CUE/M3U dependencies; reject missing tracks; preserve entries
  when a folder becomes unavailable. User preferences are restored afterward.
- `GamesSmokeInstrumentation`: original GB test ROM, rendered picture, audio,
  controller input and save-state round trip; Moonlight activity/native library
  and pairing-certificate checks.
- `ConsolePlaybackInstrumentation`: original NES, SNES and GBA fixtures through
  real JNI video/audio and save-state round trips. Fixtures/providers are debug
  only and are absent from the release APK.

Validated on Samsung SM-A526B, Android 12, arm64-v8a. PlayStation native loading
was verified; actual disc playback and Moonlight streaming require a game/BIOS
and a paired host respectively and are not covered by these synthetic tests.

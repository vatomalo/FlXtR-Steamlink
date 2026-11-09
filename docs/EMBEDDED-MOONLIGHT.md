# FlXtR's bundled Steam Link streaming client

SkyLoft compiles Moonlight Qt and FlXtR with the existing Steam Link SDK image.
The source, toolchain, build home and outputs live on `/opt/dockers/storage`.
`scripts/build-skyloft-steamlink.sh` builds Moonlight before assembling FlXtR.
The pinned upstream revision is recorded in `scripts/build-moonlight.sh` and
in the installed `moonlight/BUILD.txt`. The upstream license, modified source
archive and SHA-256 manifest accompany the package.

## Settings

Settings → Stream controls the receiving Steam Link's PC stream:

- **Automatic** uses Moonlight's normal audio-backend selection.
- **TV / HDMI** uses SLAudio with the Marvell driver selected for the child process.
- **TV / HDMI compatibility** uses SDL with the Marvell driver.
- **System output (SDL)** uses the configured SDL output.
- Additional named SDL playback outputs appear when the firmware enumerates them.
  Saved names survive enumeration reordering. A missing selected device produces
  an error rather than silently redirecting audio to another device.

The HDMI choices set both `SDL_AUDIO_DRIVER` (SDL3 firmware) and
`SDL_AUDIODRIVER` (SDL2). They do not modify `/mnt/config/system/audio_driver.txt`.
The firmware's global sink can therefore differ from FlXtR's explicit choice.
Bluetooth audio is not validated yet; an installed `bluealsa` service alone does
not establish that a paired headset is available to the selected SDL backend.

Resolution is 720p or 1080p; frame rate is 30 or 60; bitrate is 5–40 Mbps.
These preferences are applied by `packaging/moonlight-run.sh` to FlXtR's game
stream launches. They do not change movie scaling, emulator settings, or the
separately installed Moonlight application's own preferences.

`streaming.cfg` stores the quality profile. `streaming-audio.txt` stores the
output name. Updates preserve both files and Moonlight's existing pairing data
in `/usr/local/moonlight`.

An additional compressed bootstrap under `assets/` supports older updaters:
they already copy that directory recursively. The next FlXtR launch verifies
and installs the client and launcher atomically per file. The normal updater
also verifies the bundled binaries. Existing pairing and private preferences
are not included in either installation payload.

## Local audio test

Select **Test stream audio**. FlXtR releases its audio/video devices, plays a
quiet left/right test tone for approximately three seconds, then reopens the
Stream settings. This test never contacts the gaming PC. A successful exit
means that samples were submitted; the user must still confirm audible output.

Logs are `moonlight/audio-test.log`, `moonlight/audio-test-status.txt` and
`moonlight/audio-enumeration.log`. The launcher supports both the firmware's old
BusyBox `timeout -t` syntax and GNU timeout and bounds audio tests to eight
seconds. Streaming logs remain in `moonlight-gamez.log`.

Launch through Valve's app launcher or FlXtR's wrapper so that the stock Steam
Link GUI is absent while the stream owns video/audio. A direct background
launch alongside that GUI is not a valid playback test.

## SkyLoft verification

Run `python3 tests/streaming_integration_test.py` and
`python3 tests/deploy_steamlink_test.py` in the builder. The launcher tests cover
literal device/application names, HDMI driver selection, quality arguments,
invalid-profile defaults, BusyBox timeout and output-preference persistence.
The package verifier rejects incomplete bundled-client payloads.

The native Windows application does not need rebuilding for a receiving-client
audio change. Windows/Sunshine's captured audio source is a separate setting:
if the local TV tone is audible but the PC stream is silent, investigate host
capture and the Windows output selected for the game.

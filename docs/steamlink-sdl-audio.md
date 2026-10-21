# Shared Steam Link SDL audio (hardware validation pending)

## Source findings

`src/player.c` initializes SDL audio, uses FFmpeg swresample to produce 48,000 Hz
stereo signed native-endian 16-bit PCM, and opens the default output with
`SDL_OpenAudioDevice(NULL, 0, ..., 0)`, requesting 1024 frames, no callback.
`steamlink_av_sync.h` submits four bytes per stereo frame with `SDL_QueueAudio`;
it waits for 200 ms of software PCM before starting playback. Queue length feeds
the media-only audio clock and video worker. `audio_master_clock.h` estimates an
additional output period; SDL does not expose the physical HDMI/DAC position.
Neither that clock nor the 200 ms movie prebuffer belongs in an emulator driver.
Those player files are unchanged.

The pinned Valve SDK is `62b4d098d1472c3534dd098ca2a0e0e10712f1c6`.
Its `examples/retroarch/retroarch.cfg` selects `audio_driver = "sdl2"`;
its build script disables ALSA and PulseAudio. Inspection of the shipped native
SDL2 binary finds `marvell` and `Marvell HDMI audio` alongside other compiled
drivers. This establishes an available platform backend, **not** which backend
the current player's process or installed RetroArch actually selects. The SDK
snapshot inspected here does not include SDL's platform-audio implementation
source. No low-level Marvell behavior is inferred from its name. Subsequent
on-device diagnostics confirmed that native SDL selects `marvell` on the box.

RetroArch v1.7.6, commit `9750719074041e0f1949cf9535d1e04945f6213b`,
has one shared SDL/SDL2 driver in `audio/drivers/sdl_audio.c`. It opens the
legacy SDL callback API, uses an additional FIFO, returns zero in `write_avail`,
and has no `buffer_size`. RetroArch's `audio/audio_driver.c` requires both
callbacks for audio rate control. The separate adapter supplies these through
bounded SDL queue occupancy. This is a reason to test an adapter; it does not
establish that the installed driver cannot produce sound. The installed binary,
configuration and runtime maps still require device inspection. The pinned
frontend is a reproducible test target, not a claim about its installed version.
The inspected device subsequently reported v1.7.6, Git `5855391`, built in 2019;
its configuration already selects SDL2, 48 kHz, 64 ms and audio synchronization.
Its process maps confirm the private native SDL2, with no SDL3 library present.

## Implementation

`src/steamlink_sdl_audio.[ch]` uses the player's queued SDL device mechanism.
SDL selects its default backend, honoring `SDL_AUDIODRIVER` when explicitly set.
The module logs the selected driver, device list and obtained format. It fails
with SDL's error when initialization or device opening fails. It opens exactly
one device, requests S16 stereo, allows rate negotiation and balances its SDL
subsystem reference. RetroArch gets the obtained rate and retains conversion,
resampling and emulation synchronization. No core opens a device.

The adapter requests 48 kHz and a 1024-frame period. Without a named profile,
software queue capacity comes from RetroArch's existing audio latency setting,
with a minimum of one obtained SDL period. Hardware buffering is additional.
Writes return byte counts as expected by this RetroArch implementation, preserve
frame boundaries, support partial nonblocking writes, and time out on stalled
queues. Pause preserves queued data; clear and close discard it. Queue-empty
episodes are logged as **possible** underruns, not physical DAC measurements.
SDL device-stop/queue errors fail explicitly. Recovery is close then reopen via
frontend reinitialization/relaunch; there is no speculative automatic hotplug
recovery, second device or second timing clock. Calls must be serialized.
`FLXTR_AUDIO_TRACE=1` adds one log per second of submitted byte count, PCM peak
and queue occupancy. This samples one submission each second, not every sample;
nonzero PCM still does not prove audible TV output. Tracing is off by default.

## Build and opt-in

Inside the existing SDK Docker environment, from the repository root:

```sh
bash scripts/build-steamlink-audio.sh
bash scripts/build-retroarch-audio.sh
bash scripts/build-steamlink.sh
```

The package includes the diagnostic, a side-by-side `retroarch-audio/retroarch.exec`,
GPL license and corresponding patched RetroArch sources. Existing RetroArch
and the player are not overwritten. This older frontend is selected only with
`FLXTR_USE_QUEUED_AUDIO=1` in FlXtR's launch environment. The temporary append
configuration sets `audio_driver=sdl2`, 48 kHz and audio synchronization only in
that mode. Other user settings and save locations are retained. Disable the
variable to return to the installed frontend. No emulator cores are changed.

The adapter replaces only the SDL driver in the pinned build. Its exported
identifier remains `sdl2`; the startup `[steamlink-sdl]` log proves the adapter
was selected. A silent fallback to another RetroArch driver is not acceptance.

The audio build also creates `steamlink_audio_test_libretro.so`, a ROM-free
diagnostic core that alternates batch and single-sample Libretro callbacks.
It contains no SDL calls. Run it with `-L path/to/steamlink_audio_test_libretro.so`
and no content argument. A temporary configuration can use null video/input
drivers to isolate audio, then repeat with normal graphics for synchronization
testing. Do not run it alongside another player owning the media hardware.

## Device validation, in order

Stop other media sessions, then run from the installed FlXtR directory:

```sh
scripts/test-steamlink-audio 48000 64 10 1024 2>audio-test.log
```

These arguments are **test inputs**, not tuned profile recommendations. The
utility checks S16 stereo, frame rejection, queue bounds, pause/resume, deliberate
starvation, resubmission, sustained playback, drain and cleanup. It alternates
left-only 440 Hz and right-only 660 Hz tones each second. Expected intentional
pause/starvation gaps are separate from unwanted playback dropouts. Its exit
status describes API checks only. Confirm both channels are actually audible
through the television and record any crackling. Repeat at 44.1 kHz and other
required rates; record both requested and obtained rates. Repeat with longer
duration (up to 3600 seconds). Compare the private native SDL and system SDL via
`FLXTR_USE_SYSTEM_SDL=1`; capture the selected driver and process library maps.

First inspect installed RetroArch's verbose audio startup and configuration,
including per-core overrides. If its existing `sdl2` driver works, retaining it
is a valid outcome. Next opt into the queued frontend and test a lightweight NES
or GB game. Confirm logs show `[steamlink-sdl]`, obtained rate and bounded queue.
Then test ScummVM Libretro music/dialogue/effects, SNES synchronization, PSX
gameplay, MAME and FBNeo/FBA, and at least one playable PPSSPP title. Record core
version, game, duration, profile, queue-empty episodes and audible results.
ScummVM standalone is a separate frontend and is not repaired by a Libretro driver.
Finally retest the original movie player. Frame rendering alone is not a pass.

## Measured profiles

`FLXTR_AUDIO_PROFILE` accepts `low`, `balanced`, or `compatibility`. Explicit
profiles require **both** corresponding environment variables populated from
measurements:

| Profile | Software queue capacity (ms) | SDL period request (frames) |
|---|---|---|
| low | `FLXTR_AUDIO_LOW_QUEUE_MS` | `FLXTR_AUDIO_LOW_PERIOD` |
| balanced | `FLXTR_AUDIO_BALANCED_QUEUE_MS` | `FLXTR_AUDIO_BALANCED_PERIOD` |
| compatibility | `FLXTR_AUDIO_COMPATIBILITY_QUEUE_MS` | `FLXTR_AUDIO_COMPATIBILITY_PERIOD` |

No numeric profile presets are supplied without hardware data. Balanced behavior
without an explicit profile inherits RetroArch's latency request. Tune the lowest
stable queue/period with sustained tests and measured input-to-audio latency;
record queue-empty events outside startup, intentional starvation and shutdown.
Do not compensate for insufficient core performance with arbitrary huge queues.

## Validation status

On 2026-10-21, physical-device tests established:

- Standalone native SDL test: `marvell`, 48 kHz, S16 stereo, 1024-frame period,
  12,288-byte software queue cap inherited from the installed 64 ms setting.
  API checks passed and the user confirmed both channels audible and clean.
  The two empty episodes were intentional starvation and final drain.
  A separate 44.1 kHz API test obtained 44.1 kHz stereo S16 and passed;
  TV audibility for that additional short run was not separately confirmed.
- Installed RetroArch SDL driver: the corrected ROM-free fixture was audible
  and clean according to the user. Its existing output mechanism is therefore
  usable; replacing it is **not required just to obtain sound**.
- Queued adapter: the same fixture ran for 3,600 frames, with clean stereo
  confirmed by the user, zero queue-empty episodes and zero device errors.
- NES/FCEUmm: Zelda title music was confirmed audible and clean. Mario 3's test
  was silent to the user; that case remains unresolved, not a pass.
- ScummVM Libretro: Fate of Atlantis intro audio was confirmed audible and clean.
  Two queue-empty episodes were logged. Full dialogue/effects coverage and
  longer gameplay remain unverified.
- SNES/Snes9x2005: Secret of Mana Turbo crackled/dropped out according to the
  user, with **both** the queued adapter and the installed SDL driver. The
  queued run had one queue-empty episode. This is a failing core/game case,
  not evidence that arbitrary larger SDL buffers would fix it.
- SNES/Snes9x2005 Plus: the same ROM was confirmed clean through the queued
  backend. Trace logs show nonzero PCM and bounded queue occupancy. This
  comparison points to core/APU behavior, not an output-driver-only fault;
  it does not prove all Snes9x2005 games are broken. Use the Plus core for this
  case rather than masking the distortion with more buffering. No core code
  or user's per-ROM core selection is changed by this work.
- Arcade/FBA2012 Neo Geo: The Last Blade attract/demo audio was confirmed
  audible and clean. Trace logs distinguish silent boot PCM from subsequent
  nonzero audio. This does not substitute for MAME/FBNeo gameplay tests.
- PSX/PCSX ReARMed: Legend of Mana and Suikoden II did not pass. After correcting
  the scratch BIOS path, the core loaded the existing BIOS but logged repeated
  `No more registers! Abandon ship!` errors and sampled silent PCM, with repeated
  empty queues. A scratch interpreter-mode retry removed those errors but still
  sampled silent PCM and emptied the queue. Core/content diagnosis and playable
  synchronized PSX sound remain required; no larger buffer is claimed to fix it.
- MAME2003 Plus: Ghosts 'n Goblins initialized the queued driver and maintained
  bounded occupancy, but sampled PCM was mostly zero (one sampled peak was only
  2/32768) during the attract test. This
  does not establish audible MAME sound; gameplay/input and sound settings
  still need testing.
- Original FlXtR media player: after stopping the tests and resuming the
  original shell, the user launched a normal video and confirmed working sound.

Tests used null video/input drivers to isolate audio; synchronization with
visible gameplay was **not** established. A first fixture attempt failed symbol
loading, was corrected, and is excluded from audio-driver results. An initial
standalone open failed while player workers owned hardware; stopping those
workers allowed the same test to open normally. Tests use scratch configuration,
save and state paths. The installed frontends, cores and player are not replaced.

Host dummy-driver, adapter and launcher tests and ARM package builds establish
software/toolchain behavior. Measured low/compatibility presets, sustained
cross-core gameplay, PSX, MAME/FBNeo and PPSSPP remain
pending. No PPSSPP core was present in the inspected core directory. This is
not an accepted universal audio port until the remaining acceptance tests pass.

Reproduce focused host checks after preparing the pinned frontend build tree:
`bash scripts/test-steamlink-audio-host.sh`. They cover module lifecycle,
backpressure, frame alignment, failure/reopen, frontend adapter contracts,
profile validation, all 25 mandatory Libretro exports, both audio callbacks and
launcher opt-in/rollback. The module also passed ASan/UBSan checks on the host.
The existing broad `tests/games_test.py` stops at its pre-existing 480p-default
expectation (the product now defaults to native resolution); it is not counted
as a passing suite and is not changed in this work.

## Visible gameplay follow-up (not accepted as stable)

The earlier clean intro tests above used null video/input. They did not test
ScummVM speech, gameplay synchronization, or sustained gameplay performance.
PSX, MAME and PPSSPP follow-up is deferred at the user's request.

Normal FlXtR launch selected Snes9x2002 for the unpatched `SecretofMana.smc`.
The user reported very bad music despite good effects/video and suspected a
crash; the process was still alive, so a crash was not established. Subsequent
scratch runs used Snes9x2005 Plus, normal video and controller input, separate
save/state paths, and the same ROM. The user still heard gameplay crackle,
dropouts and music stutter with both installed and queued frontends.

| Test | User observation | Measured limitation |
| --- | --- | --- |
| Installed Plus, queued 64 ms / 1024 frames | Maybe slightly better than installed frontend, still stutters | 43,477 submitted output frames/s over 183 s against 48,000 required; queues empty repeatedly |
| Installed Plus, queued 64 ms / 512 frames | Slightly better | 38,520 frames/s over 94 s; queues still empty repeatedly |
| Newly compiled Plus, same audio settings | Slightly worse | Still fails sustained delivery; not a demonstrated core optimization |
| New Plus, requested 640x480 GL output | Much better overall, still dropouts; small corner picture | Native framebuffer remained 1280x720; 45,985 frames/s over 394 s |
| Process-local native framebuffer scaling, 640x480 and 960x540 | Still small corner picture | Driver logs report scaling, but physical result fails; no scaling fix claimed |
| SDL2 video with automatic fullscreen dimensions | Wrong/no picture | Invalid visible test; explicit native window dimensions need retesting |

These windows include different gameplay activity and occasional pauses/state
loads. Submission rates are diagnostic observations, not controlled benchmarks
or exact emulation speed measurements. A 64 ms capacity also does not mean
64 ms of PCM was present: frequent empty queues show insufficient delivery.
Increasing capacity alone cannot repair sustained throughput below demand.

Detailed tracing (`FLXTR_AUDIO_TRACE=1`) now records submission gaps, write
duration, total submitted frames, and queue extrema once per second. Summarize
saved logs with `python3 tools/steamlink_audio_report.py LOG...`. This does not
measure DAC delay, TV delay or physical input-to-sound synchronization.

`scripts/build-cores.sh snes-plus` now builds the pinned Plus variant with its
Blargg APU, ARMv7 Cortex-A9 hard-float/NEON compiler settings and a source
archive/build manifest. The O3 build was actually tested above and was not
faster to the user. An O2 variant was built but has not been device-tested.
No newly compiled PSX, MAME or PPSSPP core is claimed.

The diagnostic core also supports `flxtr_audio_test_mode = "av-sync"`: a
periodic white flash/click and button-triggered flash/click are emitted on the
same core frame. Host tests verify this pairing, including held-button behavior.
Physical TV/controller synchronization and ScummVM dialogue remain pending.

Scaling experiments only loaded a scratch adapter into the test process; no
installed graphics library or working FlXtR player was replaced. The native
driver accepted 640x480 and 960x540 but rejected 640x360. Its reported framebuffer,
window, EGL surface and viewport sizes did not establish correct TV scaling.
Do not enable these experiments or treat their logs as visual acceptance.

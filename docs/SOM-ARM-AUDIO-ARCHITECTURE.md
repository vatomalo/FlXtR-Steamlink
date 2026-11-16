# Secret of Mana Ultimate on the Steam Link: audio architecture

Status: 2026-11-15, branch `perf/som-steamlink-arm-stabilization-20261115`.
Measurements: `docs/SOM-ARM-PERFORMANCE-BASELINE.md`, `docs/SOM-ARM-PERFORMANCE-RESULTS.md`.

## What actually produces the "SPC-like" soundtrack

This was read from the code, not inferred from filenames:

```
som_music_bridge.py (Steam Link, bundled Python)
  follows "[som-ai] SONG 0xNN" lines in game.log
  -> title (data/som_song_names.tsv) -> genre -> yt-dlp search (tools/music.py)
  -> greenlink-audio --pcm 32040 <stream>           src/audio_player.c, at nice 19
       FFmpeg decode + swresample to s16 stereo at the core's own rate
       -> som-music-cache/pcm/<id>-32040.raw         decoded ONCE, kept (1.5 GB cap)
  -> names the complete file in /tmp/flxtr-som-music.play
som_ai_proxy_libretro.so (inside RetroArch, src/som_ai_proxy.c, src/som_music_mix.h)
  every 30 frames: re-reads the control file (track change / stop)
  every frame:     read() <= 16 KiB of the raw file into a 32768-frame (~1 s) ring,
                   page-cache reads, loops at the end of the file
  every audio batch from Snes9x: out = game + music * 176/256, saturating
RetroArch 1.7.6: sinc resampler 32049 Hz -> 48000 Hz, SDL2 driver, audio_sync on,
                 audio_latency 64 ms, rate control 0.005
PE_Single_CPU (Valve media daemon, nice -10): mixes and outputs to HDMI
```

The SNES character comes from where the music is placed in that chain, not
from any SPC processing: the arrangement is band-limited to the SNES rate
(32040 Hz, so nothing above 16 kHz survives), mixed under the game's own SPC700
effects in the same sample stream, and passes through the same RetroArch
resampler and output as the game. The native SoM music voices are suppressed by
the FlXtR core build (`retro_flxtr_skip_music_voices(2)`), so only the effects
remain from the SPC700.

**None of this changed in this sprint.** The mixer (`som_mix_apply`), gain,
rate, ring and decode settings are untouched. The only addition in the audio
path is optional timing around the existing calls (`src/som_perf.h`), and the
proxy test checks that the samples handed to RetroArch are identical with
timing on and off (`tests/som_ai_proxy_test.c`, "Frame timing").

## Why music and gameplay stutter together

There is one audio stream. The Steam Link refuses a second output while
RetroArch plays (`MV_PE_StreamCreate() failed: 0x80007065`), which is why the
music is mixed into the game's own audio. Consequences:

- Music samples are consumed at exactly the rate the emulator produces game
  samples: 534 frames of music per emulated frame (32040 / 60.0988).
- If the emulator runs below its nominal 60.10 frames/s, it produces fewer
  samples than the 48 kHz output consumes, the RetroArch/SDL buffer drains,
  and the output underruns. Game effects and music drop out **together**, and
  the picture is slow at the same moment. Pitch does not change; rate control
  only bends ±0.5 %.
- So the stutter is a CPU-budget symptom of the emulator, not a problem in the
  music decoder: with the music decoded offline since 8996759, playback costs a
  page-cache `read()` and one multiply-add per sample (`mix`/`pump` phases in
  the proxy timing).

Measured on the device, 2026-11-15 (baseline doc): the game ran at 43.6 frames/s
(73 % of nominal) with the AI bridge on, 0 % idle CPU in `top`. Music reads came
from the page cache (no disk reads, no major faults in 20 s). Fixing the speed
therefore fixes the music; making the music path "more asynchronous" would not.

## Underrun behaviour and buffering (as implemented)

| Situation | Behaviour |
| --- | --- |
| Ring empty while a track plays (file read slower than playback) | that part of the batch passes the game audio through untouched: silence in the music only, no stale samples repeated, no blocking. Counted as `music_short` in the proxy timing. |
| Control file removed (Off, game end) | ring cleared at the next 30-frame check, game audio continues |
| Track change | the new file starts at the next 30-frame check (hard cut, as before) |
| PCM file missing/short | logged once, music off, game audio unaffected |
| Emulator slower than real time | output underrun in RetroArch/SDL for both game and music (see above) |

The prompt suggested a 2 s prebuffer. The proxy's ring is ~1 s and is refilled
from the page cache every frame; a longer ring would only add memory (the
Steam Link has 256 MB and ~3 MB free, the rest page cache) without protecting
against the real failure, which is the emulator falling behind. A fade on
music underrun was not added: the file is served from the page cache (no disk
reads measured), so the ring should never run dry; the instrumented proxy
counts it (`music_short`) so this can be checked on the device instead of
assumed (results doc).

## Audio quality checks

- Sample identity of the mixing path with timing on/off: automated (proxy test).
- Mixer saturation, loop and stop behaviour: existing proxy test.
- The music files, gain and rate are unchanged, so playback speed, pitch,
  balance and loop points are unchanged by construction. Listening test after
  any proxy deploy: Potos (village theme), a field map, one battle, one map
  change (track switch); listen for dropouts, not for timbre changes.

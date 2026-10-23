# Steam Link A/V timing investigation handoff

## Saved correction

PCM refills can change `tail_us - queued_us` by one microsecond even when
no new frames have been consumed. That falsely resets the interpolation
anchor. Subtract queued sample frames from total sample frames before converting
to microseconds, so refills preserve the anchor. The focused regression measures
15,999 us of lag using the old calculation and zero lag using the new calculation
for its controlled refill sequence. This is a sample-clock correction, not a
measurement of the Steam Link's physical sound-to-picture offset.

Run:

```sh
mkdir -p build
cc -std=c99 -Wall -Wextra -Werror tests/audio_master_clock_test.c -o build/audio-master-clock-test
./build/audio-master-clock-test
```

Existing startup, interpolation, monotonicity, starvation and eight-hour precision
checks remain; the new test also checks the sample-based tail drain and freeze.
The change is wired into the native Steam Link scheduler. Desktop libmpv is not
modified. Hardware playback validation remains outstanding.

## User-supplied hardware evidence

The user's local checkout was on
`experiment/hardware-video-backpressure-20261021`; its device is reachable from
that terminal using `ssh root@192.168.10.2`, with FlXtR in
`/home/apps/greenlink`. This agent's execution container was different and its
SSH attempts returned connection refused. No deployment or device modification
was performed by this agent.

The final uploaded capture contains 70 samples from audio position 865.088 to
934.263 seconds:

| Metric | Observation |
| --- | --- |
| Submitted frames per media second | 25.0524 |
| Audio PCM reserve | 501–523 ms |
| Audio underruns / dropped frames / resets | 0 / 0 / 0 |
| Submitted PTS minus audio | -110 to +69 ms; median +43.5 ms |
| SDK hardware queue values | 1457, 1491, 1565, 1605 ms |
| Added hardware throttle wait | 15.618 ms/frame overall; 16 ms/frame after position 876 s |

The hardware queue experiment targets 500 ms and caps each wait at 16 ms. The
capture reaches that cap on every steady-state submitted frame without bringing
the reported SDK queue near its target. This warrants an A/B test with that
throttle disabled, rather than treating the SDK number as a calibrated delay.
No throttle change or fixed A/V offset is included in this correction.

`video_pts` is the last submitted compressed packet's PTS, not a displayed-frame
timestamp. B-frame reordering also makes individual submission deltas fluctuate.
`hardware_queue` is an opaque SDK metric and may include an internal buffer floor.
Neither value establishes physical display latency. The exact physical gap and
whether audio leads or trails remain unconfirmed. Raw uploaded logs are not
committed because the transcript includes unrelated provider diagnostics.

## Continue on the user's local machine

1. Verify the checkout and installed player version before changing anything.
2. Start affected playback and run `bash tools/capture-steamlink-timing.sh` on the
   local machine. It reads device state and logs twice, 60 seconds apart, without
   restarting, clearing logs or installing anything. SSH host verification stays
   enabled. Keep captured files private; older logs can contain signed media URLs.
3. Generate the known-content test with
   `python3 tools/generate_av_sync_reference.py`. It produces a 12-second H.264
   720p60 / stereo PCM Matroska clip in `build/av-sync-reference.mkv`, with white
   flashes and clicks at 2, 4, 6, 8 and 10 seconds. The decoded onsets are verified
   to differ by at most two 48 kHz audio samples. PCM avoids AAC priming/pre-echo.
4. Phone webserver playback has never worked for this user. Use SSH/SCP for the
   controlled test instead. Stop existing playback before opening another hardware
   player. Put the clip in the app's `phone-files/` directory; the player accepts
   regular files in that directory. Launch from `/home/apps/greenlink`:
   `./greenlink-player phone-files/av-sync-reference.mkv --subtitles off`.
5. Record the TV with sound in normal video mode. Keep recording audio and video
   tracks together, identify the speaker path (TV/receiver/Bluetooth), and measure
   flash-to-click separation across several pulses. The recorder can contribute
   its own A/V offset; the content onset verification alone cannot calibrate it.
6. Compare physical offset and device logs before/after the sample-clock fix and
   with/without the hardware throttle. Do not claim exact synchronization from
   software-only tests or the opaque queue duration.

The original reference clip generated during this session verified white-flash
onsets at exactly 2/4/6/8/10 seconds and audible signal threshold at one audio
sample later (0.020833 ms). No physical TV recording was received.

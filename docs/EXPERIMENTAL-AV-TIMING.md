# Steam Link audio-master timing experiment

Branch: experiment/audio-master-timing-20261021. This changes the native Steam
Link player. Desktop libmpv retains its own synchronization. The branch lives
in a separate checkout and does not deploy, publish stable updates or merge.

## Evidence

The audit covered 60 local build/playback/test logs (build/timing-log-audit.json).
Existing native test logs repeatedly hit the 40 ms pacing cap with a further
38-61 ms of waiting left. Video submission and audio feeding shared a thread.
The historical 300 ms decoder delays were deliberately injected tests.
SSH authentication to 192.168.10.2 was rejected: current physical device logs
were unavailable, so these are not device performance measurements.

With real FFmpeg 4.4, SDL dummy audio, an eight-second 30 fps H.264 B-frame clip,
44.1 kHz AAC and six injected 300 ms SLVideo stalls:

| Scheduler | Empty audio queues after startup | Elapsed |
|---|---:|---:|
| Serialized baseline 487b940 | 7-8 | 9.79-9.90 s |
| Audio-master worker | 0 | 8.17 s |

Recovery skipped 27 encoded frames via one keyframe reset, then caught up.
A decoder that blocks still freezes video during that block.

## Changes

One sample-based audio clock drives video scheduling, captions, menu time,
seek requests, checkpoints and bookmarks. Converted sample counts and SDL
queue consumption replace packet-DTS/wall-clock corrections. Fractional
microseconds do not accumulate: an eight-hour sample-clock test remains exact. Between SDL callback reports, interpolation is
bounded by the known output period and queued sample tail, so video gets a
smooth clock without imaginary progress during an outage.

SDL starts after 200 ms of PCM prefill. Queuing is bounded around 500 ms plus
one decoded frame. A drained output freezes the timeline; pause freezes audio
and the video worker. Short initial audio offsets get silence. Large timestamp
discontinuities drain old sound, reanchor the clock and recover at a keyframe.

Only the video worker submits compressed frames to SLVideo. Its queue is capped
at 128 packets and 8 MiB, alongside the existing 16/32/64 MiB network FIFO.
Controls/audio remain on the main thread. Video preserves decode order, using
DTS to schedule ahead of audio and retaining PTS for diagnostics. Late chains
are skipped only to a newer queued keyframe; overflow discards through a
keyframe and recreates the decoder rather than dropping arbitrary references.

Embedded captions use stream timestamps plus the container offset; external
captions use normalized media time. Neither uses an independent wall clock or
adds the seek offset twice. The old 700 ms external-caption linger is disabled;
cue end times, as well as starts, follow the shared clock. Adaptive quality counts actual audio underruns,
slow worker submissions and FIFO starvation when the audio reserve is low.

timing.log rotates around 1 MiB and records audio position, PCM reserve, video
PTS/delta, queue occupancy, submitted/dropped frames, resets, maximum lateness,
underruns and SDK video queue duration. Caption diagnostics omit text and URLs.

## Verification and limits

Run bash scripts/test-steamlink-timing.sh in regular WSL/Linux with Docker and
the existing flxtr-desktop-builder:local image. It compiles the pinned baseline
and experiment, generates fixtures, compares stalls, tests pause/Stop, and
checks embedded/external caption clocks including a five-second timestamp offset.
Queue tests cover bounded memory, keyframe recovery and shutdown. Prior native
RAM/preload/seek/Stop and adaptive regression tests also apply.

Measured caption transitions were 1-32 ms after their audio-clock cue starts.
A sink cannot validate physical display, HDMI audio or SDK driver threading.
SDL exposes queued PCM, not the DAC timestamp; one output period is subtracted
as an estimate. SLVideo exposes queue duration, not per-frame presentation
timestamps. The initial 100 ms decode lead and frame-rate hint require device
validation, especially variable-frame-rate sources and unusual reorder depth.
Recovery can visibly skip/freeze video. Outages longer than the audio reserve
cannot be hidden by scheduling.

## Device log capture (read only)

After reproducing, run from regular WSL:

~~~bash
ssh root@192.168.10.2 'cd /home/apps/greenlink; for f in player.log timing.log timing.previous.log adaptive-quality.log adaptive-quality.previous.log subtitle-debug.log tv-preload.log greenlink.log; do if test -f "$f"; then echo "===== $f"; cat "$f"; fi; done' > steamlink-timing-device.log
~~~

Review before sharing: older logs can contain signed provider URLs.

# Steam Link playback

All video kinds share a compressed-packet RAM queue: movies, series, anime,
uploaded/local video and MTV. Startup targets at least 15 seconds (or the saved
longer prebuffer). The default RAM cap is 32 MiB per player; Settings offers
16/32/64 MiB. The byte/packet cap or a short clip can end prebuffering earlier.
Slow networks can still starve the cache.

Forward seek uses the next complete cached H.264 keyframe at or after the
requested position. It keeps the demuxer and connection open, resets audio and
the hardware video stream, and resumes from that keyframe. A brief decoder
transition is possible. Backward/out-of-cache seeks still use the server seek
path and can buffer again.

MTV resolves one next video during current playback, then starts a second
player behind a gate. That player fills its RAM queue without opening SLVideo
or an audio device. Natural EOF promotes the prepared process. Stop, leaving
TV or quality changes cancel stale preparation. A slow/failed lookup can still
leave a gap; no gapless playback guarantee is made.

Quality is a target, not transcoding. The ARM player prefers the best compatible
8-bit 4:2:0 H.264 rendition at or below 480/720/1080p. If none exists, it chooses
the smallest compatible rendition above the target, up to 1080p. HEVC, AV1,
10-bit and 4K remain unsupported on this hardware. TV no longer clamps 1080p
back to 720p. Open Settings > Quality Target, or select Quality Target in the
playback menu and use left/right (A also advances it). Changing a YouTube clip's
quality resolves the same video again at the new target and resumes its position.

Tests cover real FFmpeg HTTP playback and seek/control paths with a test-only
SLVideo sink, plus RAM FIFO integrity and preloading lifecycle. Actual display,
sound and transition smoothness still need physical Steam Link testing.


## Automatic quality

Select Settings > Quality Target > AUTO (also in the playback menu after 1080p).
New installations start in Auto at 720p. Existing saved manual presets stay
manual until Auto is selected. Desktop editions retain manual quality controls.

The ARM player measures FIFO wait time after initial buffering, hardware
frame-submission delays, and audio queue underruns. These are stutter proxies,
not a count of physically displayed or dropped frames. Startup and deliberate
pause/seek transitions get a 10-second grace period. A fatal decoder rejection
can request a lower rendition immediately. A newly switched player must run
for 10 seconds before it is considered settled for initialization rollback.

Within a 10-second window, 750 ms of RAM FIFO starvation, three decoder
submissions taking at least 250 ms (or three frame intervals), or three audio
underruns can request a lower rendition. The player tries 480p first, then
720p only if that is the lowest available step below 1080p. If the source
has one fixed rendition, it logs the floor and keeps playing.

Stable upgrades require at least 12 seconds of video ahead in the RAM cache:
45 healthy seconds to try 720p from a lower rendition, 90 healthy seconds to
try 1080p. No upgrade is attempted in the last 30 seconds of known-duration
content. A struggling tier and all higher tiers get a five-minute backoff
before another stable probe; new titles clear the tier backoff.

A switch currently reopens playback near its current timestamp; it is not a
seamless HLS rendition swap. It may briefly buffer. Failed initialization or
YouTube resolution attempts fall back once to the prior tier. Sources must
offer a compatible H.264 rendition; this feature does not transcode fixed
files. Auto health and decisions are recorded in adaptive-quality.log (rotated at 1 MiB),
player.log and greenlink.log. The persistent health log survives player restarts.

# Video sizing and Music (Steam Link beta)

Built on `28a54fe` (PR #20), preserving the existing bookmark, artwork, search,
subtitle and provider changes.

## Video size

Fit keeps the picture's aspect ratio, Stretch fills the display, and 1:1 centers
the encoded pixel dimensions without enlarging them. The previous adapter called
`MV_PE_VideoSetUnderScanWindow`, which rejected the non-fullscreen rectangles.
The adapter calls `MV_PE_VideoSetScaling`, retaining the firmware's input
window and supplying the desired output rectangle in display coordinates.
Using encoded dimensions for the input crops the picture. The initial output
rectangle may include underscan (the device reported 38,21 / 1844x1037 on a
1920x1080 display); it is not the full coordinate space. Scaling destinations
through that inset adds unwanted borders and shifts the picture. Regression
tests cover removing this inset, centered Fit/1:1, readback and restoration.
The previous scaler state is restored on player exit.
Missing symbols, rejected calls or mismatched readback report failure and retain
the previous size. This is a private Marvell ABI, tested on the user's firmware.

## Music

Press **R1** at the library (or **M** / click the top-right Music label).
The screen replaces the old “NATIVE / MINIMAL” label. **Start** searches;
the controller keyboard uses **A/Cross** to type and **B/Circle** to delete.
**Start** submits. A physical keyboard can type or paste with Ctrl+V.
Choose a track with **A/Cross**; **X/Square** pauses; **B/Circle** stops and exits.
Tracks advance in result order after normal completion. Errors stop the queue.

Paste a public `https://open.spotify.com/playlist/...` URL in the search field
to import the track/artist names visible in Spotify's public embed (up to 100).
No Spotify API key or Spotify playback is involved. Private playlists and items
not exposed by the public embed cannot be imported. Each track name becomes a
YouTube search; the actual YouTube title appears under “YouTube match”. Matches
can differ from the Spotify recording.

yt-dlp 2026.08.19 and QuickJS resolve audio links in a short-lived worker. Python
bytecode is prepared on the build host. After resolution, only the native audio
player remains, decoding AAC/Opus to stereo PCM with about three seconds of queued
audio. No browser, server daemon or PC is needed. Three recent search results are
cached for two hours. Result rows and worker deadlines are bounded; cancellation
terminates the worker process group, including its JavaScript helper.

## Build

On the Docker host, run `bash scripts/build-music-runtime.sh`, then copy
`build/music-runtime.tgz` into the SDK checkout's `build` directory. The runtime
uses verified Alpine ARM packages and an isolated musl loader; it does not replace
firmware libraries. The official yt-dlp zip is checked against its pinned SHA-256.
Only the YouTube extractors and imports needed by yt-dlp are bundled.

Inside the SDK checkout, run `bash scripts/build-player.sh` followed by
`bash scripts/build-steamlink.sh`. The package includes the video player, native
audio player, Music worker and runtime. The update script verifies the new files
and repairs an older updater's incomplete Music installation. If an old updater
installs only the new UI, pressing Music completes that migration automatically.

Music is currently packaged for Steam Link. Linux development can use installed
Python/yt-dlp/QuickJS for metadata tests; a Linux audio-player package is not added
by this change.

## Validation

`make test-music-layout` covers scaler readback/rollback, source coordinates,
playlist metadata validation, bounded TSV output and cache eviction.
`python3 tests/update_package_test.py` verifies existing integrity/repair behavior
plus missing Music components and modified runtime repair.

Device checks: all three sizing modes read back correctly and were visually
confirmed by the user; yt-dlp starts in about three seconds; live YouTube search,
audio link resolution and a short native AAC playback succeeded.

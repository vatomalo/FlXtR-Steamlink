# Playback investigation — 2026-10-05

## Update — 2026-10-06

- The user confirmed visible movie playback and sound on the original hardware.
- SDL_HideWindow left the library over video. The shell now destroys the renderer,
  window and video subsystem during playback, then recreates them afterward.
- Optional Marvell viewport interception enables fit/stretch/1:1 requests without
  reading private SLVideo object layouts. The device accepted/read back the fit
  rectangle. Subsequent remote playback attempts hit occupied/unavailable hardware
  audio/video resources; all viewing modes still need a visual test via the launcher.
- Native metadata requests on the Steam Link successfully returned seasons and
  episodes. Poster HTTPS initially failed with the old CA store; the pinned current
  Mozilla bundle resolves that validation failure while keeping verification on.
- The popular-list crawl reached the API's 500-page limit for both types and yielded
  8,911 unique movies and 9,849 unique series. It contains no playback links. Those
  counts are a dated snapshot, not a claim that all entries are playable.
- Controller browsing/search and season/episode/source screens are implemented.
  Sources can have multiple server/quality entries keyed to the exact episode.
  Automatic signed-request/WASM source resolution is still not implemented.

## Observed

1. Flixer serves a React browser UI with HLS.js in its player bundle.
2. Its source resolver loads client JavaScript and a WebAssembly module. The client
   obtains server time, signs requests and processes responses before selecting a
   media source. The ordinary player URL is not a video URL.
3. One title was opened through the normal website player. Its observed media
   playlist was checked using FFprobe from WSL, without adding cookies, Referer or
   other custom headers. FFprobe successfully opened the HLS playlist and reported:

   - H.264 High profile, 1920x1072, yuv420p (8-bit 4:2:0)
   - 24000/1001 fps
   - AAC-LC, 44100 Hz, stereo
   - timed ID3 metadata (not required for playback)

   The transient playlist URL is intentionally not stored in this repository.
   This verifies one source, not every title/server, long-term URL lifetime, or
   seeking/subtitles.
4. The user's Steam Link is reachable over SSH and runs ARMv7 Linux. SLVideo,
   SLAudio, SDL2 and GnuTLS are present. The pre-existing `/usr/bin/ffmpeg` fails
   to start because `libavdevice.so.52` is absent.
5. Valve's `SLVideo.h` exposes an H.264 hardware stream. Low-latency mode only
   supports I/P frames; the prototype deliberately uses normal mode for movie
   B-frames. Moonlight's low-latency submission loop must not be copied unchanged.
6. The new 1.4 MB ARM player was uploaded to the Steam Link. All its dynamic
   dependencies resolved. Its `--probe` mode opened that same Flixer HLS source
   **on the Steam Link itself**, with TLS verification enabled, and confirmed
   H.264 1920x1072 and an audio track. This does not require a PC proxy.
7. The shell ran headlessly on the actual device for 60 frames without crashing.
   Screen output was deferred while an existing Moonlight session was running.

## What this supports

A native client can plausibly play at least this tested source using FFmpeg
demuxing/audio and SLVideo H.264 video. No transcoding or desktop streaming should
be necessary for compatible media. Actual rendering, audio and timing require
on-device validation.

## What remains separate

Automatic title-to-URL resolution is not implemented. A browser-resolved URL can
be placed in a private catalog to test the player. Shipping a browser engine or
Node runtime on the Steam Link would undermine the requested footprint. A native
resolver needs an independent compatibility investigation; a PC-side helper would
instead introduce a PC dependency and must be clearly identified as such.

Unsupported codecs, 10-bit video, DRM-protected media and expired/session-bound
URLs cannot be assumed to work. The prototype rejects incompatible video; it
does not silently start expensive software video decoding.

## Primary references

- https://github.com/ValveSoftware/steamlink-sdk
- https://github.com/moonlight-stream/moonlight-qt/blob/master/app/streaming/video/slvid.cpp
- https://flixer.gd/assets/js/VideoPlayer-52536286.js
- https://flixer.gd/assets/js/WatchPartyOverlay-52536286.js
- https://ffmpeg.org/releases/ffmpeg-4.4.5.tar.xz

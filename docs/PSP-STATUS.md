# PSP streaming port: not released

The Linux beta is not a PSP executable. There is no FlXtR `EBOOT.PBP` or PSP
auto-update channel yet. Do not install the Linux or Steam Link payload on a PSP.
Direct online streaming is the requirement; an offline-only demo would not meet it.

## Backend investigation, 2026-10-08

The user's `garden-gaiden-psp-sdk` Docker image contains PSP GCC 15.2.0, SDL2,
curl with mbedTLS, and the firmware MPEG/AAC interfaces. The inspection/build
container is `flxtr-psp-native`; the existing Garden Gaiden container is unchanged.

The strongest reference found is [Tilefinch](https://github.com/stjanovitz/tilefinch),
inspected at commit `32bb34f05d637088bc230db9226a307f976565db`. It has an MIT-licensed
native HTTPS/HLS/MP4 stack and a PSP firmware AVC/AAC backend. Its documented
official media envelope is compatible 240p/360p Baseline/Main MP4 and 240p HLS,
not arbitrary H.264 streams. See its `src/media_backend_psp.c`,
`src/media_hls.c`, `src/media_mp4.c`, `src/psp_media_hls.c`, and
`include/tilefinch/media_backend.h`.

Upstream tests PSP-3000. PSP Go and PSP-2000 are expected to work but not qualified.
Its complete browser does not support PSP-1000's 32 MB. PSP Street lacks Wi-Fi.
Extracting a small media backend could have different memory requirements;
all-model support must not be claimed from the browser's results.

A cross-build of that reference in the existing toolchain failed: the default
vendored QuickJS fingerprint does not match its declared pin. Selecting its
alternate QuickJS-NG build then fails with incompatible pointer types under
PSP GCC 15.2.0. These are reference-browser build results, not a FlXtR port.
No modified reference code is bundled or published as FlXtR.

[PMPlayer Advance](https://github.com/DavisDev/pmplayer-advance) was also inspected.
It provides older hardware MP4/MKV playback and NetHostFS support, but is not a
drop-in HTTPS/HLS backend for the current sources.

## Integration still required

1. A PSP application entry point and controller/UI loop without Linux fork/exec.
2. Native HTTPS source lookup, with PSP-compatible JSON and crypto dependencies.
3. A bounded HLS/MP4 demuxer feeding firmware AVC/AAC, with explicit codec/profile,
   decoded-dimension and memory checks before playback.
4. A persistent buffer and PSP Go `ef0:` / Memory Stick `ms0:` support.
5. A PSP-only verified update channel and recoverable EBOOT installation.
6. Actual PSP Go network, decoder and updater testing.

The live KissAnime sample currently supplies 1080p, 720p and **854x480** video.
The manifest advertises the lowest rendition as 640x360, so manifest dimensions
cannot be trusted as a decoder compatibility check. None of those tracks fits
Tilefinch's documented small-stream envelope. Native PSP code must find a
compatible rendition or report it unsupported; choosing “low” cannot transcode
video on the PSP.

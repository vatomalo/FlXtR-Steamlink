# Optimization branch

This branch is for measured improvements before promotion to main. Android CI
builds an artifact for this branch and skips the stable update channel.

## First pass

- Public catalogue response cache: 128 fixed slots instead of 16, with the same
  six-hour TTL, URL collision checks, response-size limit and atomic writes.
  Existing cache files remain safe to read; collisions are cache misses.
  At most 128 response files plus their lock files are used. This is a disk cache,
  not a new in-memory allocation. Responses are limited to 512 KiB before JSON encoding.
  Signed playback URLs, credentials and private companion responses stay uncached.
- PC cover requests reuse the worker's curl handle, retaining connection/TLS/DNS
  caches between search, metadata and image requests. Per-request state is reset;
  HTTPS, certificate validation, response bounds and timeouts remain enabled.
- SDL polls pending foreground workers every 25 ms rather than waiting as long as
  250 ms to notice completion. SDL events still wake input immediately; this is
  a worker-completion improvement. Idle polling remains unchanged.
- Android keeps up to 4 MiB of decoded game covers per library screen. Revisiting
  a cover avoids another file read and bitmap decode. Closing the screen clears
  the cache and cancels queued work; image URL tags reject stale asynchronous results.
- Android software video reconfigures its native window only when dimensions
  change. Cleanup resets the remembered dimensions before another core/window.
  Pixel format conversion, game timing and hardware-rendered video are unchanged.
- Desktop downloader offers PCSX ReARMed alongside Beetle PSX. Users can install
  and select it through existing core choices. Existing defaults and per-ROM
  preferences are retained; no existing savestates are converted.

## Dynarec audit

PCSX ReARMed advertises `pcsx_rearmed_drc=enabled` by default when built with a
recompiler; PPSSPP advertises `ppsspp_cpu_core=JIT`. The Android frontend already
uses the core's advertised defaults. A core compiled without a recompiler cannot
be accelerated by inventing an option. No new recompiler has been compiled in
this first pass, and no emulation-FPS improvement is claimed without hardware tests.

Sources: [PCSX options](https://github.com/libretro/pcsx_rearmed/blob/master/frontend/libretro_core_options.h),
[PPSSPP options](https://github.com/hrydgard/ppsspp/blob/master/libretro/libretro_core_options.h).

## Reproduce checks

On SkyLoft, from this worktree, use the desktop builder to run:

```sh
make -j2 all test-response-cache test-settings test-rom-menu
python3 tests/core_manager_test.py
python3 tests/gamez_catalog_test.py
```

The deterministic cache test writes 96 distinct public-response fixtures then
revisits them: the former 16-slot layout retains 16, the new layout retains 70.
This means 54 additional revisits avoid fetching in that fixture; it is not a
claim that all browsing is 4.4 times faster. Expiry, corrupt entries, hash
collisions and size limits are tested too.

Android CI must compile both JNI ABIs, Java, lint and unit tests. Physical phone
and Steam Link validation is still required for frame pacing, core throughput
and end-to-end startup times. Compare the same ROM, core, device, output mode
and save state, with cold and warm caches separately.

## Next measurements

Measure initial menu time, cold/warm catalogue page time, cover completion time,
ROM launch time, average frame time and missed-frame counts. Prioritize the
largest measured delays. Preserve existing game sessions, save integrity and
platform/core compatibility while evaluating further prefetching, GPU conversion
or core-specific optimizations.

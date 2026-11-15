# Secret of Mana Ultimate on the Steam Link: performance baseline

Measured 2026-11-15 on the live device while the user's game session ran
(Secret of Mana Turbo, AI bridge on, soundtrack mixing on, living-world
service on), unless a row says otherwise. Nothing in this document is
estimated unless marked "estimate".

## Device

| Item | Value (how measured) |
| --- | --- |
| Address | `root@100.124.74.13` (still correct; hostname `steamlink-1DD4`) |
| SoC / CPU | Marvell MV88DE3108, **one** ARMv7 Cortex-A9 core (CPU part 0xc09, r3p0), 995 BogoMIPS, NEON + VFPv3 (`/proc/cpuinfo`) |
| Clock / governor | not exposed (`/sys/devices/system/cpu/cpu0/cpufreq` absent); left alone |
| Thermal | no `/sys/class/thermal`; throttling not observable |
| Memory | 269,604 kB total, ~3-6 MB free, ~133-139 MB page cache, no swap (`/proc/meminfo`, `free`) |
| Kernel | Linux 3.8.13-mrvl #190 PREEMPT (no ARM vDSO: `clock_gettime` is a syscall, 0.52 us measured) |
| Storage | rootfs yaffs2 (ro); `/home/apps` ext4 on loop0 over USB `sda2`; `/tmp` tmpfs 131 MB |
| Tools present | `perf` 3.8.13 (cycles sampling works, no tracepoints / call stacks), `top`, `chrt`, `nice`, `ionice`; no `pidstat`, `strace`, `taskset`. Nothing was installed. |

## Software

| Item | Value |
| --- | --- |
| RetroArch | 1.7.6 (Git 5855391), `retroarch.exec`, 14 threads |
| Drivers | video `gl` (Vivante GAL), audio `sdl2` at 48000 Hz, input `sdl2` |
| Sync (FlXtR "performance" profile, `game_performance=0`) | `video_threaded=true`, `video_vsync=false`, `audio_sync=true`, `audio_latency=64`, rate control 0.005, sinc resampler quality 2, no shaders/rewind/run-ahead |
| Core | `snes9x2005_plus_flxtr_libretro.so` sha256 `fbe04cd7…121a`; frameskip disabled, overclock disabled, low-pass off; audio input 32049.26 Hz (RetroArch log) |
| Proxy (before this sprint) | `som_ai_proxy_libretro.so` sha256 `1e901f19…d679` = reproducible build of 6b0ddb2 (rebuilt and compared byte for byte) |
| Nominal cadence | core reports 60.0988 frames/s (NTSC); `video_refresh_rate=59.94`; vsync off, so pacing is by audio sync |

## Where the CPU went (before)

System-wide `perf record -a -F 200`, 15 s, AI on with the shell-loop transport:

| Share of sampled cycles | Who |
| ---: | --- |
| 30.5 % | Snes9x core (inside RetroArch) |
| ~24 % | rest of RetroArch: kernel 8.6 (DMA cache maintenance, softirq), libc 5.4 (memcpy/memset), GAL/gal3d/GLES 4.5, RetroArch 2.7, SDL 1.6, proxy 0.95 |
| 10.2 % | `PE_Single_CPU` (Valve media daemon, nice -10: audio/video output) |
| 6.7 % | `tailscaled` (userspace WireGuard carrying the AI's SSH traffic) |
| 6.3 % | busybox `gzip` (AI WRAM reads) |
| ~6 % | short-lived forks: `[`, `printf`, `pidof`, `wc`, `tail`, `sh`, dynamic loader |
| ~4 % | wifi SDIO irq, Bluetooth, HID, kworkers |

`[` and `printf` are external commands on this busybox (`type [` -> `/bin/[`),
so the AI's persistent shell loop forked for every request. Fork rate: 16/s.

Per-process CPU from `/proc/<pid>/stat` over 30 s: RetroArch 49.9 %,
PE_Single_CPU 13.3 %, tailscaled 6.5 %, ksdioirqd 2.7 %, AI shell loop 1 %,
plus the forked children; idle 7.4 %.

## Game speed (before)

Frames counted from the proxy's own frame numbers in `game.log` and from the
game's WRAM frame tick (7E:00F4; the two agreed within 1 %):

| Condition | Game frames/s | % of 60.0988 | CPU idle |
| --- | ---: | ---: | ---: |
| AI on, shell-loop transport | 43.6 | 73 % | 7.6 % |
| AI bridge stopped for 40 s (same session) | 52.5 | 87 % | 12 % |

The game never reached full speed in either case. Music came from the page
cache (no `pgpgin`, no major faults in 20 s), so it was not causing I/O stalls.
The RetroArch main thread was runnable ~94 % of sampled instants and waited on
the GPU (`gckOS_WaitSignal`) in ~5 %; it took ~1,170 context switches per
second, almost all involuntary (preempted by higher-priority threads).

## Repeated benchmark (tools/som_perf_benchmark.py)

See `docs/SOM-ARM-PERFORMANCE-RESULTS.md` for the 3 x 90 s runs per scenario
and their raw files in `artifacts/som-perf/`.

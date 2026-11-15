# Secret of Mana Ultimate on the Steam Link: results (2026-11-15)

Baseline and device facts: `docs/SOM-ARM-PERFORMANCE-BASELINE.md`. Raw data:
`artifacts/som-perf/*.json` (summaries in the `.txt` next to each). All runs on
the real Steam Link during real play, so the scene changed between runs; only
runs from the same session and place are compared with each other.

## A. Root cause

1. **The emulator runs below real time on the single Cortex-A9 core** (44.0 of
   60.1 frames/s with the AI on). The soundtrack is mixed into the game's only
   audio stream, so when the emulator falls behind, the output underruns and
   game and music stutter **together**. The music path itself is cheap and was
   not the cause: tracks are decoded once offline, read from the page cache (no
   disk reads, no major faults), and the music ring never ran dry
   (`music_short` = 0 in every instrumented run).
2. **The AI's transport cost ~20 % of the core**: the device-side shell loop
   forked `[`, `printf` and `gzip` per decision (busybox has no `[`/`printf`
   builtins) and moved ~14 KB per decision through SSH and Tailscale's
   userspace WireGuard. Stopping the bridge alone raised the game from 44.0 to
   53.5 frames/s.
3. **The PC <-> Steam Link Tailscale path is relayed** through DERP Helsinki
   (36-47 ms, "direct connection not established"; the two machines are on
   unrouted networks 192.168.10.0/24 and 10.3.0.0/24), so tailscaled spends
   CPU on TLS and WireGuard for every AI byte.
4. **Remaining: RetroArch's video hand-off takes 6-7 ms of every frame**
   (proxy timing, `video` phase), on top of 8-10 ms of core emulation, against
   a 16.7 ms budget. The main thread waits on the GPU there
   (`gckOS_WaitSignal`), which is why ~10 % of the CPU stays idle while the game
   is slow.

## B. Changes

| Commit | What |
| --- | --- |
| cf25815 | `tools/som_wram_server.py`: device-side observation server, same requests and replies as the shell loop (tested byte for byte), plus `dlt` block deltas of the full snapshot; `RemoteLink` uses it, falls back to the shell loop |
| b8ea466 | living-world service through the server's restricted `living` profile |
| 7076094 | `src/som_perf.h` + proxy hooks: optional frame timing (off by default) |
| d4cbaa8 | `tools/som_perf_benchmark.py`: reproducible sampling + comparison |
| 1c3ead6 | decorated frames copy 114 KB of visible pixels instead of 229 KB (0.16 vs 0.6 ms measured) |
| fdf2814 | opt-in nice value for RetroArch's threads (`cores/experimental/som_ai_priority`) |
| 97d7c9b, e1dd207 | deploy/rollback scripts, docs, raw results |

Not changed: emulated CPU clock, frameskip (stays disabled), audio rate,
resampler, music gain/decoding, ROM patches, game logic, AI decisions.

## C. Performance comparison

Same session, pre-sprint proxy 6b0ddb2, game tick from WRAM (3 x 90 s unless noted):

| Scenario | Game frames/s | vs D-shell | CPU idle | forks/s | tailscaled |
| --- | ---: | ---: | ---: | ---: | ---: |
| D-shell: AI on, old shell transport | 44.0 (44.6/45.2/42.3) | - | 7.9 % | 19.9 | 6.8 % |
| D-server: AI on, observation server | 48.1 (48.0/48.5/47.9) | +9 % | 10.3 % | 9.4 | 5.5 % |
| D-server-living: + living world on server (2 x 60 s) | 50.6 (50.7/50.5) | +15 % | 10.5 % | 8.9 | 4.1 % |
| B: AI bridge stopped | 53.5 (53.5/53.4/53.5) | +22 % | 11.4 % | 8.4 | 2.2 % |

AI decision rate: 2.05/s before, 2.81-3.10/s after (journal), so the AI got
faster while costing less. Per request on the device: ~23 ms CPU and ~14 KB
(shell, counting the full snapshot every 6th read) vs ~7 ms and ~1.9 KB (server).

New proxy fdf2814 with timing on (fresh boot, different scenes; G = one
session, alternating priority, 60 s each):

| Run | Emulated frames/s | interval P50/P95/P99 ms | core / video / audio / proxy mean ms | late frames |
| --- | ---: | --- | --- | ---: |
| G nice -5 #1 | 51.7 | 18.6 / 28.1 / 35.5 | 9.7 / 6.7 / 0.45 / 0.7 | 361 |
| G nice 0 #1 | 54.1 | 17.0 / 29.0 / 41.8 | 7.7 / 7.6 / 0.53 / 0.7 | 402 |
| G nice -5 #2 | 53.4 | 17.6 / 27.0 / 48.0 | 8.6 / 7.2 / 0.52 / 0.7 | 325 |
| G nice 0 #2 | 52.4 | 17.3 / 30.5 / 43.0 | 9.3 / 6.9 / 0.46 / 0.7 | 346 |
| F (combat scene, 3 x 90 s) | 43.7 / 42.4 / 52.6 | up to 22.0 / 33.8 / 43.3 | up to 14.7 / 6.2 / 0.5 / 0.8 | 468-1246 |

Percentiles are 0.25 ms bucket upper bounds; "late" = interval > 1.5 x 16.69 ms.
Nominal cadence from the core: 59.92-60.10 frames/s.

**Priority:** an earlier live test (old proxy, 2 x 60 s each) showed 48.7 -> 50.1
frames/s at nice -5, but the interleaved same-session test above shows no
difference beyond scene variance. It is enabled on the device at the user's
request and is reversible (delete `cores/experimental/som_ai_priority`); it is
**not** a demonstrated gain.

**Acceptance criteria:** not met. 99 % of nominal speed and zero underruns were
not reached; the game is at ~50-54 frames/s in field scenes and ~42-44 in heavy
combat. Instrumentation overhead was not measurable live (the game tick did not
advance during the timing-off run); estimate 12 clock reads x 0.52 us per frame.

## D. Audio preservation

The music chain (`docs/SOM-ARM-AUDIO-ARCHITECTURE.md`) is unchanged: same
files, 32040 Hz, gain 176/256, same mixer. The proxy test asserts that the
samples handed to RetroArch are identical with timing on and off. The "SPC-like"
character comes from band-limiting to the SNES rate, mixing under the real
SPC700 effects, and from the fact that the bridge's genre fallback often picks
uploads of the original 1993 OST (e.g. `qPMVVAOtEjM`, "A Prayer and a Whisper |
Secret of Mana OST 1993").

## E. AI compatibility

- 450 Python tests pass (`PYTHONPATH=tools python3 -m unittest discover -s tools
  -p 'test_som_*.py'`), including 13 new server tests: byte-identical replies for
  every plain request, identical inflated bytes for compressed ones, marker/STOP
  takeover, action/message validation, delta resync after a lost session,
  fallback to the shell loop (NOSERVER, or a server that never starts).
- `make test-som-ai-proxy test-som-ai-player2` pass (timing, packed frame copy,
  priority parsing).
- Live: both services connected through the server, decisions continued at a
  higher rate. Every AI read is now a complete current snapshot instead of a hot
  patch on a base up to six reads old.

## F. Deployment

| When (CET) | File | SHA-256 | From |
| --- | --- | --- | --- |
| 12:07 | `/home/apps/greenlink/tools/som_wram_server.py` | `086bd92c…c8a0` | cf25815 (manual, recorded) |
| 12:41 | same | `45027d92…e229a` | fdf2814 (deploy script) |
| 20:46 | `/home/apps/retroarch/cores/experimental/som_ai_proxy_libretro.so` | `94430a09…c3f` | fdf2814 (game closed by the user) |
| 20:46 | `cores/experimental/som_ai_priority` = `-5` | - | user choice |

Previous proxy kept as `som_ai_proxy_libretro.so.pre-armperf-20261115-204656`
(`1e901f19…d679`). Full snapshot in `~/backups/som-steamlink/`.

**Unexplained reboot:** the Steam Link rebooted at about 21:01, after the
install, while the game was closed. Only one game launch happened since the
20:44 session (log rotation proves it), and it started after the boot, so no
RetroArch process had loaded the new proxy before the reboot. The cause is
unknown (no persistent kernel log on the device). The post-boot session ran
normally with the new proxy.

## G. Remaining bottlenecks (measured)

1. RetroArch video hand-off: 6-7 ms per frame (`video` phase).
2. Core emulation: 8-10 ms per frame in field scenes, up to ~15 ms in combat.
3. PE_Single_CPU (Valve media daemon, nice -10): 13-14 % CPU.
4. tailscaled: 4-5 % with the AI on (DERP relay).
5. Proxy's own work: 0.7 ms per frame mean (not yet broken down beyond pump/wram).

## H. Next recommendations (impact / risk)

1. **Try the FlXtR "balanced" game profile** (`game_performance=1`: no threaded
   video, vsync on) and benchmark it against "performance": the 6-7 ms video
   hand-off is the largest non-emulation cost. A user setting, no code; one game
   restart per test.
2. **Get a direct Tailscale path** (e.g. a fixed UDP port for the Steam Link's
   tailscaled, or a route between the networks): would remove the DERP TLS
   layer. A networking change, so it needs the owner's decision.
3. Break the proxy's 0.7 ms down (combo/evade/ring-AI ticks, WRAM export of
   152 KB every 6 frames; the hot file is unused once the server is in place).
4. Decide on the priority file based on longer interleaved runs.
5. Frameskip and emulated clock changes remain off the table unless the above
   fail, per the brief.

# Deploying the Steam Link performance work

Branch `perf/som-steamlink-arm-stabilization-20261115`. Two independent pieces:

| Piece | Where it runs | Needs a game restart | Rollback |
| --- | --- | --- | --- |
| `tools/som_wram_server.py` (observation server) | Steam Link, one Python process per AI / living-world SSH session | no: bridges pick it up on their next SSH session (restart the PC services) | move the file aside, or `SOM_AI_SHELL_LOOP=1` for the AI bridge |
| `som_ai_proxy_libretro.so`, `som_weapon_masks.bin`, and `som_ai_weapon_masks_on` | inside RetroArch | yes: refused while RetroArch runs the proxy | restore the prior files or roll back |
| PC side (`som_qwen_bridge.py`, `som_living_world.py`) | SkyLoft services, run from this worktree | restart `flxtr-som-ai` / `flxtr-som-living-world` | `git checkout` the previous commit, restart |

## Procedure

```bash
# 0. tests
PYTHONPATH=tools python3 -m unittest discover -s tools -p 'test_som_*.py'
make test-som-ai-proxy test-som-ai-player2

# 1. build (Steam Link SDK; ARMv7 EABI5, -Os, no CPU-specific flags)
bash scripts/build-som-ai-proxy.sh

# 2. install; the script refuses the proxy while the game runs
bash scripts/deploy-som-arm-performance.sh server     # any time
bash scripts/deploy-som-arm-performance.sh proxy      # with the game closed; installs core and ROM-derived masks
systemctl --user restart flxtr-som-ai flxtr-som-living-world
```

`deploy-som-arm-performance.sh` does, in order: checks the device is the
ARMv7 Steam Link with the expected tree; for the proxy, checks the file is an
ARM EABI5 build and that no `retroarch.exec` has `som_ai_proxy_libretro.so`
loaded; takes a full read-only snapshot with `scripts/som-backup-steamlink.sh`
(core, launcher pieces, feature files, saves, SHA-256 of everything, ROM by
checksum only) under `~/backups/som-steamlink/<stamp>/`; uploads to
`<target>.armperf-new`; compares SHA-256; copies the old file to
`<target>.pre-armperf-<stamp>` on the device; copies permissions; renames
atomically on the same filesystem; appends `stamp git-sha path sha256 backup`
to `cores/experimental/backups/som-arm-perf-deploy.txt`. Proxy deployment
also installs the ROM-derived weapon mask pack and creates its opt-in marker.

It never touches saves, ROMs, RetroArch configuration, the AI memory database
(on the PC) or other FlXtR files, and restarts nothing on the device.

## Rollback

```bash
bash scripts/rollback-som-arm-performance.sh --dry-run   # show what it would do
bash scripts/rollback-som-arm-performance.sh
```

For each file in the deploy record since the last rollback it restores the
state before the first install (the `.pre-armperf-` copy, via copy + atomic
rename so the backup remains), or moves a newly added file aside
(`.rolled-back-<stamp>`). It removes the timing marker and logs a `rollback`
line. A restored proxy is used from the next game launch. Restart the PC
services afterwards so they reconnect (they fall back to the shell loop when
the server file is gone).

Immediate kill switches without rollback:

- AI transport: `systemctl --user set-environment SOM_AI_SHELL_LOOP=1 &&
  systemctl --user restart flxtr-som-ai` (undo with `unset-environment`).
- Proxy timing: it is off unless `/tmp/flxtr-som-perf-enable` exists or
  `FLXTR_SOM_PERF=1`; `rm /tmp/flxtr-som-perf-enable` stops it within 10 s.

## Turning the timing on and reading it

```bash
ssh root@100.124.74.13 'touch /tmp/flxtr-som-perf-enable'
ssh root@100.124.74.13 'cat /tmp/flxtr-som-perf.json'      # one window per 600 frames
python3 tools/som_perf_benchmark.py --scenario D --runs 3 --seconds 120
```

The benchmark creates and removes the marker itself. Raw output:
`artifacts/som-perf/`.

## Deployment log

See `docs/SOM-ARM-PERFORMANCE-RESULTS.md`, section "Deployment", for what was
installed, when, from which commit, and with which checksums.

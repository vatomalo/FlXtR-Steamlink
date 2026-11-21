# Secret of Mana Ultimate: build, validation and deployment checklist

Branch `feature/som-disciplines-controls-20261114` (never merged to `main` from here).
Installed state and its read-only snapshot: `docs/SOM-DEVICE-INVENTORY.md`.

## 1. Dependencies

| what | version used (SkyLoft, 2026-11-15) |
|---|---|
| Debian | 13 |
| host C compiler | gcc 14.2.0 (`cc`) |
| Python | 3.13.5, standard library only for the tests |
| Steam Link SDK | `/opt/dockers/storage/toolchains/steamlink-sdk` (or `STEAMLINK_SDK_PATH`), armv7 gcc 9.2 |
| ssh | BatchMode key access to `root@100.124.74.13` (only for headless runs and deployment) |
| SNES core on the device | `snes9x2005_plus_flxtr_libretro.so` (music-voice patch), stock `snes9x2005_plus` as fallback |

## 2. Tests (no ROM needed)

    make test-som-ai-proxy test-som-ai-player2 test-som-combo test-som-songs test-som-net-pad
    cc -std=c11 -Wall -Wextra -Werror -Isrc tests/som_disciplines_test.c -o /tmp/d && /tmp/d
    PYTHONPATH=tools python3 -m unittest discover -s tools -p 'test_som_*.py'
    cc -std=gnu99 -Wall -Wextra -Werror -o /tmp/h tools/som_headless/som_harness.c -ldl
    cc -Os -std=c99 -Wall -Wextra -Werror -fPIC -shared src/som_ai_proxy.c -o /tmp/p.so -ldl

The same run in CI: `.github/workflows/som-combo-checks.yml`.

## 3. Headless real-ROM tests (user-supplied ROM and save; nothing installed)

    R='/home/apps/retroarch/roms/Snes/Secret of Mana, Turbo.smc'
    S='/home/apps/retroarch/roms/Snes/Secret of Mana, Turbo.srm'
    for s in title_start field_load dodge clones no_free_xp; do
      python3 tools/som_headless/run.py --host root@100.124.74.13 --rom "$R" --start "$S" \
        tools/som_headless/scenarios/$s.txt; done
    python3 tools/som_headless/run.py --host root@100.124.74.13 --rom "$R" --start "$S" \
      --out ~/.local/share/flxtr/som-fixtures tools/som_headless/scenarios/to_rabites.txt
    for s in combat_rabites clones_rabites; do
      python3 tools/som_headless/run.py --host root@100.124.74.13 --rom "$R" \
        --start ~/.local/share/flxtr/som-fixtures/rabites.state tools/som_headless/scenarios/$s.txt; done

## 4. Build

    bash scripts/build-som-ai-proxy.sh     # -> build/som-ai/som_ai_proxy_libretro.so (ARM, -Werror)

Release candidate, source `src/` at commit `358acb7`:
`a7c041917304b11732017053dbd7544965f004c3942c004145aaa146df6bcaf1`.
Parity with the installed build `8bb510b3...` (`strings` of both): nothing removed except
five log lines that printed a literal `\n`; additions are progression, perks and the
title-screen fix.

Soundtrack mixing adds a second artifact, `greenlink-audio` with the `--pcm` mode, built
like `scripts/build-player.sh` does (static ffmpeg 4.4.5, Steam Link SDK):

    $CC -Os -std=c99 -Wall -Wextra -Werror -I"$cache/install/include" $(pkg-config --cflags sdl2) \
      src/audio_player.c -o build/greenlink-audio-arm -L"$cache/install/lib" \
      -lavformat -lavcodec -lswresample -lavutil $(pkg-config --libs sdl2 gnutls) -lm -lpthread -ldl -lz

Its normal (SDL) mode is unchanged, so it can replace `/home/apps/greenlink/greenlink-audio`
for all FlXtR music. Installing the mixing feature = proxy + `greenlink-audio` +
`greenlink.sh` + `tools/som_music_bridge.py` (snapshot first, game closed, swap by rename as
in section 6). Checksums of the current candidates are printed by the build; record them at
install time.

## 5. Release gates for this candidate

| gate | state |
|---|---|
| intended branch, committed, pushed | yes |
| automated tests | all pass (C suites, 431 Python tests, 1 real-ROM test skipped without fixtures) |
| production proxy compiled with the Steam Link toolchain | yes, checksum above |
| documentation matches behaviour | yes (`SOM-DISCIPLINES-CONTROLS.md`, `SOM-MUSIC-BRIDGE.md`, `SOM-AI-HANDOFF.md`) |
| no experimental memory writes enabled by default | unchanged from the installed build; new code only reads SRAM |
| install + rollback written | below |
| save-data risk | SRAM is never written. Save states grow by 1036 bytes (see rollback) |
| hardware results labelled | section 7 |

## 6. Deployment plan (needs the player's approval; not done)

1. Game closed: `ssh root@100.124.74.13 'ps | grep -c "[r]etroarch.exec"'` must print 0.
2. Snapshot: `bash scripts/som-backup-steamlink.sh` (new timestamped folder, checksummed).
3. Copy and verify, then swap by rename (a running process keeps its old file):

       ssh root@100.124.74.13 'cat > /home/apps/retroarch/cores/experimental/.proxy.new' \
         < build/som-ai/som_ai_proxy_libretro.so
       ssh root@100.124.74.13 'cd /home/apps/retroarch/cores/experimental &&
         sha256sum .proxy.new && chmod 755 .proxy.new &&
         cp -p som_ai_proxy_libretro.so som_ai_proxy_libretro.so.pre-stabilize-20261115 &&
         mv .proxy.new som_ai_proxy_libretro.so'

   The printed checksum must equal section 4.
4. Player launches the game. First field entry logs `PROGRESS slot 1 loaded ...: no record,
   fresh progression`; to keep the XP earned so far, once, with the save loaded:
   `ssh root@100.124.74.13 'printf "progress_claim_legacy\n" > /tmp/flxtr-som-ai-action'`.
5. The SkyLoft AI service runs from this checkout: it picks up the Python changes
   (curiosity, Qwen failure handling) at its next restart. Restarting it is the player's call.

## Rollback

Game closed, then
`ssh root@100.124.74.13 'cd /home/apps/retroarch/cores/experimental && mv som_ai_proxy_libretro.so.pre-stabilize-20261115 som_ai_proxy_libretro.so'`
(or restore from the snapshot's RESTORE.txt). **Save states written by the new build are
1036 bytes longer**: the old build hands them to the core unchanged and the core may refuse
them, so after a rollback the exit auto-state may not load (the game starts at the title;
SRAM saves are unaffected). Restore the snapshot's `.state.auto` together with the binary if
resuming matters. Progress records in `som_progress/` are simply ignored by the old build.

## 7. Verification status

| feature | unit/CI | headless real ROM | Steam Link hardware |
|---|---|---|---|
| title Start reaches the game | yes | yes | pending |
| class label next to the HUD portrait | yes | yes | reported by the player |
| proxy-driven dodges, no barrel | yes | yes (48 px, no 0x0400) | partly: 22 dodges in the last log |
| hit blocked during a dodge | yes | no (monster timing) | pending |
| combo ladder, finisher | yes | yes (4 hits, level 1 finisher) | pending |
| Kage Bunshin clones, explosion, substitution | yes | yes | pending |
| discipline XP, fading, level-up | yes | yes | yes (log: Ninjutsu level 2) |
| progression per save, state trailer, legacy claim | yes | yes (slot detection, trailer round trip, old state) | pending |
| perks | yes | regression only | pending |
| exit auto-state full size | yes | yes | yes (531668 bytes after the player's exit) |
| soundtrack bridge chain | yes | mixing verified (rms, pacing) | search/stream yes; separate player blocked (MV_PE_StreamCreate); mixing pending install |
| AI curiosity, Qwen failure handling | yes | n/a | pending (service restart) |

For weapon-mask builds, record directional coverage from the local preview.
On hardware, compare the installed core inode with the running RetroArch
mapping before evaluating the HUD or weapon art. Capture masked and native
fallback frames on the same save, including a transition from a covered
animation pose to an uncovered one.

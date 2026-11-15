# Secret of Mana Ultimate: installed state (read-only inventory, 2026-11-15 03:30)

Taken with non-mutating commands while the game was running (one RetroArch process, the
soundtrack bridge beside it). Nothing was restarted, written or copied onto the device.
Snapshot of every file below (except the ROM):
`~/backups/som-steamlink/20261115-033024/` on SkyLoft (MANIFEST.txt, SHA256SUMS,
RESTORE.txt; read-only), made by `scripts/som-backup-steamlink.sh`.

## Steam Link `root@100.124.74.13`

| item | path | sha256 |
|---|---|---|
| AI proxy core (= build of `3f1b155`) | `/home/apps/retroarch/cores/experimental/som_ai_proxy_libretro.so` | `8bb510b365ba8ced738e0293da9c7f45c846395ba863cab70149635a79625bc6` |
| SNES core used by the proxy (snes9x2005 + music-voice patch) | `.../experimental/snes9x2005_plus_flxtr_libretro.so` | `fbe04cd7e80275fee47319d7185d581384e7dab8bfa64af550421af7f0d4121a` |
| stock fallback core | `/home/apps/retroarch/cores/snes9x2005_plus_libretro.so` | `0ecbc47910edb9cd425d1671f5ed84ad064957caa75bf51b98f621885b348ab1` |
| RetroArch 1.7.6 | `/home/apps/retroarch/retroarch.exec` | `90ab58a6ae60a9179c5633965b33b9f5d7d873fa876142a227fc573da2916d17` |
| ROM (4194304 bytes: no copier header) | `/home/apps/retroarch/roms/Snes/Secret of Mana, Turbo.smc` | `455e90a2c37534630d90eff7ad7abce8f9346d63280ef2c70ec9aa829f4041f4` |
| SRAM save (8192 bytes) | `.../roms/Snes/Secret of Mana, Turbo.srm` | `c9c43588...` (at 03:27:36) |
| exit auto-state (531668 bytes: full, the 4 KB truncation is fixed live) | `.../roms/Snes/Secret of Mana, Turbo.state.auto` | `f6d27d58...` |
| launcher (with soundtrack hook) | `/home/apps/greenlink/greenlink.sh` | `5597b5814c6b...` |
| soundtrack bridge | `/home/apps/greenlink/tools/som_music_bridge.py` | `b07ca1f39156...` |
| savestate shadows | `/home/apps/greenlink/tools/state_guard.py`, `state-shadows/` | `7070066f21c3...` |

Feature and settings files in `.../cores/experimental/`: `som_ai_disciplines` (v1, per
device), `som_ai_gambits`, `som_ai_combat`, `som_music_genre` (4 = Piano),
`som_song_names.tsv`, `som_ai_rom_patch.txt` (3 memory-only byte patches applied at load),
`som_summon.bin`, markers `som_ai_living_world_enable`, `som_ai_summon_anyone` (present),
`som_ai_idle_autostart_disable` (absent: idle auto-start on). Earlier proxy builds:
`som_ai_proxy_libretro.so.pre-*` / `.bak-*` and `backups/20261115-30c8c69/`.

## SkyLoft

- `flxtr-som-ai.service` (user unit, active since 03:27:41) runs
  `scripts/run-som-ai-service.sh` **from this checkout**: committed Python changes reach the
  live AI the next time the service restarts.
- Settings `~/.config/flxtr/som-ai.env` (keys: HOST, MODEL, OLLAMA_URL, INTERVAL,
  START_TIMEOUT, OBJECTIVE; not copied into Git); memory `~/.local/state/flxtr/`.
- `flxtr-som-remote-pad.service` active (Player 2 join page).

## Evidence found in the device logs (previous session)

`game.previous.log`: 22 `XP` awards with the expected fading (10, 8, ... 3) and a Ninjutsu
level-up, 22 dodges, 6 bashes, 10 specials, 1 exit auto-state. `som-music.log`: the bridge
found and resolved a Piano arrangement, but `greenlink-audio` failed with
`MV_PE_StreamCreate() failed: 0x80007065` (see the soundtrack section of
`docs/SOM-RELEASE-CHECKLIST.md`).

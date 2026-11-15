# Headless regression harness (real ROM, no person needed)

- `som_harness.c`: a minimal libretro host. Loads a core (normally the AI proxy), a ROM and
  a start file (save state, or `.srm` battery SRAM), runs a scenario script and exits 0 / 1
  (an `expect` failed) / 2 (setup error). Commands are listed at the top of the file.
- `run.py`: builds a private proxy copy whose writable paths point into a throw-away run
  folder (the player's settings, saves and the live game's `/tmp` command files are never
  touched), runs a scenario on the Steam Link (`--host`) or locally, checks
  `#! expect-log` / `#! expect-no-log` lines, fetches screenshots as PNG into
  `build/som-headless/` (ignored by Git) and deletes the run folder. Timeout: exit 3.
- `scenarios/`: title Start, field load + save-state round trip, dodge, clones, no free XP.

Requirements: Python 3, a C compiler; for `--host`, the Steam Link SDK
(`STEAMLINK_SDK_PATH`, or `/opt/steamlink-sdk`) and BatchMode ssh. The ROM and save are
user-supplied and only read (copied into the run folder).

    python3 tools/som_headless/run.py --host root@100.124.74.13 \
      --rom '/home/apps/retroarch/roms/Snes/Secret of Mana, Turbo.smc' \
      --start '/home/apps/retroarch/roms/Snes/Secret of Mana, Turbo.srm' \
      tools/som_headless/scenarios/dodge.txt

ROM-free checks: `PYTHONPATH=tools python3 -m unittest tools/test_som_headless.py`
(the real-ROM test runs only with SOM_HEADLESS_HOST, SOM_HEADLESS_ROM, SOM_HEADLESS_START).
Headless results are not hardware play: they use scripted input and no monsters are near
the save's inn.

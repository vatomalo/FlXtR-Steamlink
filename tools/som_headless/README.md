# Headless regression harness (real ROM, no person needed)

- `som_harness.c`: a minimal libretro host. Loads a core (normally the AI proxy), a ROM and
  a start file (save state, or `.srm` battery SRAM), runs a scenario script and exits 0 / 1
  (an `expect` failed) / 2 (setup error). Commands are listed at the top of the file.
- `run.py`: builds a private proxy copy whose writable paths point into a throw-away run
  folder (the player's settings, saves and the live game's `/tmp` command files are never
  touched), runs a scenario on the Steam Link (`--host`) or locally, checks
  `#! expect-log` / `#! expect-no-log` lines, fetches screenshots as PNG into
  `build/som-headless/` (ignored by Git) and deletes the run folder. Timeout: exit 3.
- `scenarios/`: title Start, field load + save-state round trip, dodge, clones, no free XP,
  the game's own cartwheel (`miss_cartwheel`: E1E5 poke only; `cartwheel`: monk A via the proxy).

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

## Boss training

Use `--train-boss` in place of a scenario and pass an existing state from
inside the boss encounter. The trainer starts attempts with the Boy, Girl,
and Sprite in turn. Its first strategies keep the saved classes and try
their Y and X attacks at range, including Ninja's shuriken and
Weaponmaster's bow. Further trials can pair a ranged fighter with a Cleric
companion. It ranks attack patterns by observed boss HP, keeps
working on party members without a win, and stops when all three have led
a win with the whole party alive or after 18 attempts per party member.
Training can spend the party's existing Candy on the most injured ally.
Knockouts lower the score.

Training reloads the original state bytes for each attempt. It runs in a
disposable directory with exit autosave disabled and records the results in
`run.log`. It neither writes a savestate nor changes the running game.

    python3 tools/som_headless/run.py --train-boss --host root@100.124.74.13 \
      --rom '/home/apps/retroarch/roms/Snes/Secret of Mana, Turbo.smc' \
      --start /path/to/boss.state --timeout 600

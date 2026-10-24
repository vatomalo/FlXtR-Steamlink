# Per-ROM settings

Select a game and press **Y**. Use **LB/RB** or **Tab** to switch between
**Core**, **Cover** and **Cheats**. A game still starts directly when selected
normally; these tabs do not add a start-game confirmation.

## Core

Press A to choose an installed core. A saved ROM choice takes priority over the
console default in Settings > ROMS. Automatic inherits that console default.

## Cover

Choose another game from the imported library and press A to borrow its cover.
The current choice is marked SET. Automatic Cover removes the override.
Left/right moves by eight rows. Changes refresh the library when leaving the
menu. The donor's artwork reference is copied, so the target's name, ROM file,
console and launch core stay the same. Overrides are keyed by ROM path identity,
not temporary import IDs, and survive library refreshes and restarts.

## Cheats

Select Download / Change Cheat File to browse the closest matches from the
[official Libretro database](https://github.com/libretro/libretro-database/tree/master/cht).
Choose the correct game, region and revision; downloading initially disables
every code. Then select individual codes and press A to turn them on or off.
Changes apply on the next game launch. B returns from file selection to the
code list. Left/right moves by eight rows.

The database's arcade files are under FBNeo - Arcade Games. Code compatibility
depends on the chosen game and core; a download is not a compatibility test.
[Libretro documents](https://docs.libretro.com/guides/cheat-codes/) both emulator
handled codes and RetroArch memory cheats. Both sets of file fields are retained.

Each ROM keeps a separate `.cht` file under `cheats/`, and replacing a file keeps
the previous version as `.cht.previous`. Nothing automatically enables all
downloaded codes. System indexes are cached for one day; network tasks run in
a cancellable worker rather than blocking controller navigation.

The launcher copies the selected cheat file into a private per-ROM runtime
database using the chosen core's real library name and ROM filename. It enables
RetroArch's apply-after-load setting in the per-launch append config. The global
RetroArch configuration, game saves and save states are not rewritten.

## Validation on Steam Link

The installed RetroArch 1.7.6 was exercised with a small headless test core using
null video, audio and input drivers. The actual launcher prepared the runtime
cheat path; RetroArch called `retro_cheat_set` for an enabled test code, and made
no cheat callback after the code was disabled. Both runs exited normally.
This validates loading and enable/disable integration, not every game's codes.

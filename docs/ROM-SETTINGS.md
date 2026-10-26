# Per-ROM settings

Select a game and press **Y**. Use **LB/RB** or **Tab** to switch between
**Core**, **Cover**, **Cheats** and **States**. A game still starts directly when selected
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

## States: slots and recovery

The States tab selects a persistent manual slot (0–99) for this ROM. With no
override it displays RetroArch's existing default. During gameplay, hold Back
and press R1/RB to save the selected slot, or L1/LB to load it. Back+Start still
exits. Automatic resume uses `.state.auto` independently of numbered slots.

The Steam Link launcher seeds shadows for existing states, protects the current
game before launch, and runs a separate watcher for saves made during gameplay
and on exit. It covers numbered states, slot zero and automatic states beside
ROMs and in configured state folders, including external `/mnt/disk/roms`.
Shadows live under `state-shadows/` and do not rename the original files.

Each file retains its initial shadow, current shadow and previous distinct
version. The checksum is verified when restoring. A file that shrinks from
more than 8 KB to 4 KB or less is retained as a suspect copy without replacing
the larger current shadow. Initial copies are not proof that an old state was
valid: existing truncated data cannot be reconstructed.

Choose a shadow and press A twice to recover it. B cancels the confirmation.
Recovery refuses while RetroArch is running, preserves the replaced primary
as a separate version, then atomically restores the checked bytes. Restoring a
numbered state selects its slot; use Back+L1/LB in the game to load it. Restoring
an automatic state does not change the existing Game Auto Resume preference.
State format compatibility still depends on using the originating game/core.

Shadows preserve bytes; they do not fix RetroArch 1.7.6's autosave flush bug.
The headless native test saved and loaded a complete 16 KB slot-7 state, verified
its live shadow and recovered its prelaunch version. The same runtime wrote
only 4 KB for the test's 16 KB exit-time automatic state. Upgrading or patching
that runtime remains separate work.

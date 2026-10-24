# Automatic savestate investigation — Steam Link, 2026-10-24

The installed RetroArch identifies itself as 1.7.6 (Git 5855391). FlXtR's
Settings > ROMS > Game Auto Resume already writes savestate_auto_save and
savestate_auto_load to the per-launch append config; both are currently true.
This is an exit-time snapshot, not a periodic checkpoint. RetroArch's
`autosave_interval` is for SRAM and is a separate mechanism.

Read-only inspection found these existing auto states at exactly 4096 bytes:
- /mnt/disk/roms/Snes/Secret of Mana, Turbo.state.auto
- /mnt/disk/roms/Snes/Legend of Zelda, A Link to the Past Redux.state.auto
- the imported PSP Crisis Core .state.auto under app/roms/PSP

The Secret of Mana log reports a 4096-byte input and a 531668-byte core state,
then explicitly reports failure to load. The selected core is
snes9x2005_plus_flxtr. The previous PSP log reports a 41943040-byte core state.
The files are suspicious; their lengths alone do not prove when/how they broke.
Existing core changes can also invalidate a state's serialization format.
No saves, SRAM, preferences or emulator binaries were changed during this audit.

The official RetroArch changelog records a fix in 1.8.9 for auto savestates not
being fully flushed when quitting without first closing content:
https://github.com/libretro/RetroArch/blob/master/CHANGES.md
This matches the installed version and symptoms, but is not yet a reproduced
root cause. Disk space is available (roughly 11 GB on /mnt/disk).

Next controlled work:
1. Back up a test game's existing .srm and .state* files while it is stopped.
2. Reproduce save and load using the same ROM and exact core binary; check exit
   status, final file size, and a visible restored game position.
3. Compare 1.7.6's exit path with the upstream flush fix. Test a patched private
   runtime before replacing the installed emulator; preserve the native SDL path.
4. Separate future snapshots by stable ROM identity and core identity, retain a
   previous known-good snapshot, and expose failed resume instead of treating
   'found auto state' as successful load. Do not migrate/overwrite legacy files
   until a successful load with their originating core has been verified.
5. Verify cold launch, normal Back+Start exit, app restart, disabled auto resume,
   core changes, and disk write failure. Forced termination cannot guarantee an
   exit-time save and should never replace a previously verified snapshot.

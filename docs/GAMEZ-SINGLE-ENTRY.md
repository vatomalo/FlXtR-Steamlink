# FlXtR Gamez: single Sunshine entry

## Decision (2026-10-29)

Use **one persistent Sunshine application**, named `FlXtR Gamez`, rather than
registering, editing and deleting a Sunshine application for each PC game.

The existing Final Fantasy VII Remake Sunshine entry must remain available
until the new flow has been tested end to end.

## Target flow

1. FlXtR Gamez lists game titles from a data-driven catalog. Each entry has a
   stable ID and a validated Windows executable path (never shell text).
2. Selecting a title sends its stable ID to a dedicated companion on the trusted
   Windows host (CafeLeBlanc) over an authenticated LAN endpoint.
3. The companion resolves the ID against its allowlisted game catalog and
   stores a single-use launch request for the `FlXtR Gamez` session. It must
   not accept arbitrary process paths or shell commands from network clients.
4. Steam Link opens `FlXtR Gamez` with Moonlight after receiving the
   companion's acknowledgement.
5. Sunshine preparation executes the existing `QuitExplorer.bat`, starts
   `F:\apps\VitaBar\VitaBar.exe`, and optionally applies the working
   `Playnite_1080` display settings.
6. The Windows companion starts the selected game's executable with the correct
   working directory and supervises its lifetime.
7. Cleanup shuts down only the VitaBar instance started by this session,
   restores Explorer with the existing `StartExplorer.bat` script, and resets
   the display resolution if it was changed. Cleanup must be idempotent and
   include recovery after a crash or abandoned session.
8. Moonlight exits, returning to Games -> Gamez in FlXtR.

## Discovery

Scan these existing library roots, when present:

- `A:\Gamez`
- `B:\Gamez`
- `D:\Gamez`
- `E:\Gamez`

Discovery should identify likely game launchers, filter support executables and
installers, and allow user review/overrides for ambiguous directories. It must
not auto-launch unreviewed executables. Preserve stable IDs across rescans.

## Integration contracts

- One static Sunshine application named `FlXtR Gamez`.
- The Gamez Windows companion owns Windows process launching, Explorer recovery,
  and VitaBar lifecycle. Moonlight owns only streaming.
- An authenticated, versioned request/acknowledgement protocol bridges the
  Steam Link launcher and Windows companion. Authenticate requests; bind only
  to the LAN; reject unknown game IDs and overlapping sessions.
- The selected-game request must be acknowledged before launching Moonlight.
  Timeout, authentication failure, or unknown game ID should leave the user in
  FlXtR with an error message, not start an unrelated game.
- Treat the 69 existing hardcoded Gamez entries as a temporary test inventory,
  not proof that executables were found or Sunshine entries registered.
- Keep the legacy FFVII launch working until the single-entry pipeline passes
  launch, controller, exit, cleanup and crash-recovery tests.

## Status

**Design only.** The Windows companion, authenticated protocol, game discovery,
single-entry Sunshine configuration, and Moonlight routing are not implemented
by this commit. Do not change the device's Sunshine or Steam Link deployment
based solely on this document.

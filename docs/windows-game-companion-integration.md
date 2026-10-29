# FlXtR Gamez: Windows companion integration

Target branch: `feature/gamez-ff7-moonlight-20261029`.

## Separation of responsibilities

- **FlXtR (Steam Link/ARM):** Moonlight discovery, display, and launching host applications.
- **Sunshine (Windows):** Streaming host and app registration.
- **FlXtR Gamez (Windows):** Discovery/approval of installed PC games, Sunshine sync, and launch management.
- **XInputOSK (Windows):** A separate controller-driven overlay process for emergency navigation and quitting even if the game launcher or sync service fails.

The Steam Link ARM build must **not** compile or link Windows/XInput source.

## Existing uploaded XInputOSK baseline

Source supplied in `XInputOSK(1).zip` (Visual Studio C++ project). Import only source and project assets, not the `.vs/` directory, `Debug/`, `*.obj`, `*.pdb`, `*.ipch`, or other build intermediates.

Current mappings: Start+Back toggles the keyboard; D-pad navigates; left shoulder + stick moves the mouse; X/B click in mouse mode; triggers + stick move the overlay.

### Required overlay corrections before release

1. Start+Back currently toggles on **every polling tick** while held. Make it rising-edge-triggered, with a key-release latch; no repeat while held.
2. Provide a **controller-accessible close action** for the foreground game. First request `WM_CLOSE`, and after a configurable delay allow an explicitly confirmed force termination targeting **only the game process**, never Explorer, Sunshine, FlXtR Gamez, or XInputOSK.
3. Keep the overlay in a **separate process** from the game and sync daemon. Make startup idempotent so Sunshine launching it cannot spawn duplicates.
4. Use `WS_EX_TOPMOST` and focus restoration as best-effort. Exclusive fullscreen, elevated windows, graphics-driver hangs, and OS hard locks cannot be guaranteed to be overlayable.
5. Support both a Start+Back recovery shortcut and a controller-only way to return to desktop. Avoid trapping focus or globally killing unrelated processes.
6. Do not silently elevate the whole companion; report launch elevation requirements clearly.

## Sunshine sync contract

- Resolve the actual Sunshine host using a configured base URL and authenticated API; never blindly assume localhost or a default `apps.json`.
- **Read current apps first**, match by stable companion identifier / normalized launch target, and preserve all unmanaged apps.
- Maintain explicit states: discovered, approved, synced, verified, failed.
- Create/update approved games via Sunshine's documented API for the installed version and **read them back** to verify success. Handle authentication, CSRF/version differences, and unavailable host without losing approvals.
- No automatic deletion of Sunshine apps. Only remove companion-managed entries with explicit user approval.
- Filter redistributables, installers, crash reporters, Java runtime tools, server utilities and multiple EXEs from the same game; permit manual overrides.
- Keep separate logs for game discovery, Sunshine registration, launch, and overlay recovery.
- Persist scan roots and approvals; start once with Windows.
- FlXtR should request a refreshed Moonlight app list after successful sync.

## Verification checklist

1. Start XInputOSK with Sunshine offline: overlay still opens.
2. Hold Start+Back for one second: exactly one toggle.
3. Freeze a test game: normal close then confirmed force quit works; Windows remains controllable.
4. Start companion twice: only one overlay is active.
5. Sunshine offline: approved games persist for retry, no catalog corruption.
6. Sunshine online: register one game, verify it appears through GET apps and in Moonlight/FlXtR.
7. Re-run sync: no duplicates. Existing manually managed apps remain untouched.
8. Windows build using Visual Studio; Steam Link Linux/ARM build unaffected.

## Status

This file defines the integration contract and regression criteria. The uploaded XInputOSK sources have **not yet been imported into GitHub or rebuilt**; do not treat this document as a completed companion implementation.

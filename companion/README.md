# FlXtR Gamez Windows companion

Reimplements the supplied XInputOSK C++ utility as a .NET Windows Forms app.
Source inspected: `XInputOSK/XInputOSK/XInputOSK.cpp` in the supplied ZIP.
The archive's binaries and Visual Studio caches are not distributed.

## Build and install

For the prebuilt Windows ZIP, extract it and run this from the extracted folder;
no SDK or build commands are needed:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\Install-Gamez.ps1
```

The commands below apply only to a source repository checkout.

On Windows with the .NET 9 SDK, from the repository root:

```powershell
dotnet build companion/FlXtR.Gamez -c Release
dotnet run --project companion/FlXtR.Gamez.Tests -c Release
dotnet publish companion/FlXtR.Gamez -c Release -r win-x64 --self-contained true -o companion/publish
powershell -File companion/Install-Gamez.ps1
```

The installer copies to `%LOCALAPPDATA%\FlXtR\Gamez\app` and enables a per-user
Windows sign-in entry. It does not require an administrator or alter Sunshine.
Quit the running companion before replacing installed files.
To uninstall, quit from its tray menu, delete the `FlXtR Gamez` value under
`HKCU\Software\Microsoft\Windows\CurrentVersion\Run`, then remove the app folder.
Catalog and settings are retained in `%LOCALAPPDATA%\FlXtR\Gamez\catalog.json`.

## Discover and review

The app scans A:, B:, D:, E: `\Gamez` roots at startup and every five minutes. Use **Scan libraries**
after installing games. Missing drives are skipped. Discovery reads PE headers,
rejects DLLs, filters installers/support tools, skips reparse points, and never
executes a candidate. IDs derive from the normalized executable path and remain
stable across scans; a move creates a new candidate. Every executable starts
disabled. Select the actual game executable and approve its full path. Multiple
candidates are deliberately separate so launchers can be reviewed independently.
Discovery reads only executable headers, reports the current path and count, and
offers **Stop scan**, retaining candidates already found. It does not hash entire
game binaries. Approval computes SHA-256 once; launch always verifies the full
hash again. A size or modification-time change revokes approval on rescan.
Existing approvals from older versions need one fresh review to record metadata.

**Open settings / overrides** opens the local catalog. Quit the companion before
editing and restart afterward. Set `Title`, `Id`, `Cwd` and `Args` as needed; only local
settings may specify paths or arguments. Override an ID to `1` for the existing
FFVII Gamez tile, `2` for Ghost of Tsushima, etc., matching `packaging/greenlink.sh`.
IDs must be unique ASCII letters/digits, maximum 32 characters. For a filtered
launcher, add a local entry with `Approved: false`, then approve through the UI.
Executables must remain in one of the four library roots. Working directory defaults
to the executable's directory; `Cwd` may override it. Missing games remain listed and cannot launch.

## Controller controls

- Start + Back toggles the non-focusing OSK and mouse mode once per press.
- D-pad selects a key; A types it; X backspaces; B sends Escape.
- Shift/Ctrl/Alt/Win are sticky until the next key, or until the overlay closes.
- Hold LB and move the left stick for mouse movement; X left-clicks, B right-clicks.
- Y resizes the OSK; hold both triggers and move the left stick to reposition it.
- LB + Y opens Gamez. Use mouse mode for its buttons and approval dialogs.
- LB + RB + Back requests the supervised game's normal window close.

All four XInput slots are checked. Inputs are passed through when navigation
mode is off. Closing the main window keeps the tray companion running. Game exit
requests are graceful; games can prompt to save. Elevated applications and secure
Windows dialogs cannot receive input from a normal user app. Launchers that detach
child processes are not yet tracked: select the real game executable where possible.

## Steam Link and Sunshine

### Connect the existing companion to FlXtR

`Connect-SteamLink.ps1` is a one-time administrator setup for CafeLeBlanc
(`192.168.10.187`) and the Steam Link (`192.168.10.2`). It uses the existing
installed companion and catalog. It backs up the catalog, maps the unique
**Final Fantasy VII Remake** entry to FlXtR game ID `1`, fingerprints that
selected executable, and preserves all other game approvals. It enables the
authenticated listener for the Steam Link, starts the companion invisibly at
sign-in through a scheduled task with **Limited** run level, and reuses the
existing `Desktop_1080` Sunshine entry for streaming. Administrator rights are
used for network setup, not for running the companion or launching games.

The script uploads private connection settings with SCP. Enter the Steam Link
root password in the SSH prompt; the token is not printed or published. Codex
then validates and activates those settings on the device. This does not
install a second game-launching service or start Playnite.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Connect-SteamLink.ps1
```

Use `-StartupOnly` to correct an older elevated startup without modifying game
approvals, the network listener or private device settings. An existing private
`steamlink-setup/known_hosts` file is reused for SCP with strict checking.

Direct FFVII launch from FlXtR was confirmed on CafeLeBlanc and Steam Link on
2026-11-09. Other discovered games remain unapproved until separately configured.

The sections below describe manual configuration and other deployments.

The existing Python bridge is retained for compatibility, but do not run it on
the same port as this app. Configure the .NET companion's `Bind` to the Windows
host's LAN IP and `AllowedClients` to the Steam Link IP. `Token` is generated on
first run; keep it private and copy the same value into the Steam Link token file.
The default is loopback only. For a LAN listener, an administrator must reserve
the HTTP URL for your Windows user, for example:

```powershell
netsh http add urlacl url=http://192.168.10.187:47777/ user="$env:USERDOMAIN\$env:USERNAME"
```

Restrict a Windows inbound firewall rule for TCP 47777 to the Steam Link address.
The bearer-token HTTP bridge is for a trusted private LAN; no internet exposure.
Set `GAMEZ_HOST=192.168.10.187:47777`, the private `GAMEZ_TOKEN_FILE`, and
`GAMEZ_SUNSHINE_APP='FlXtR Gamez'` in the existing device `gamez-bridge.conf`.
Authenticated `GET /v1/games` returns approved IDs/titles;
`POST /v1/launch` takes an ID as its raw body. Paths and shell commands are never
accepted. Unknown/unapproved/changed games reject; overlapping live processes
return 409. HTTP 200 acknowledges successful process creation, not gameplay readiness.
The current Steam Link UI retains its existing 69 tiles; use numeric ID overrides
to connect discovered games to those tiles. Dynamic tile synchronization remains
future work.

Keep the original **Final Fantasy VII Remake** Sunshine entry untouched. Create
a separate **FlXtR Gamez** Desktop-style application. In its preparation command,
run the installed `Start-Gamez.ps1` in the signed-in user's session:

```text
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "C:\Users\vatom\AppData\Local\FlXtR\Gamez\app\Start-Gamez.ps1"
```

This starts the companion on demand; the named mutex makes repeated launches
harmless. Keep Explorer running for keyboard-free navigation. Existing VitaBar,
display and Explorer hooks may be copied manually only if needed; this app does
not change those settings or claim ownership of existing processes.
The device currently sends the game request before opening Sunshine, so Windows
sign-in startup must be enabled for that route. If the companion is stopped,
first open the Sunshine Gamez desktop session to start it, then select the game.
Without `gamez-bridge.conf`, the existing per-app route (including FFVII) remains.

## Validation limits

The console tests cover executable filters/headers, root restrictions, authentication,
client restriction, invalid IDs, oversized bodies and rejection of unapproved games.
Controller feel, Sunshine user-session startup and real game exit require a Windows
controller and Steam Link end-to-end check. No live Sunshine configuration is modified
by the build or tests.

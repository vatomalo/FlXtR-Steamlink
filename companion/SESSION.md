# Managed game streaming

Install `Install-Session.ps1` in Administrator PowerShell on the Windows game
host, with Sunshine running and the game closed. It preserves the catalog,
backs up Sunshine's applications file, installs the Windows package after its
native child-process and command-line tests pass, and configures the companion
to run as the signed-in user with Limited privileges.

The managed Sunshine application is **FlXtR Gamez**. Its prepare command checks
that an approved game session exists; its main command waits for that session;
its undo command restores Explorer. Existing Sunshine applications are preserved.
Switch the Steam Link's private `GAMEZ_SUNSHINE_APP` to this name only after
Moonlight discovers the installed application.

FlXtR's GAMEZ folder reads the companion's authenticated `/v1/games` endpoint
each time it is opened. Only approved entries are shown, with their original
string IDs carried through selection and launch. Approving or disabling a game
in the Windows launcher updates the list on the next open; no Sunshine entry
per game is required. An unavailable companion shows no old static game links.
The companion checks approval and the executable hash again before launching.
PC cover art is matched against Steam's public title search and downloaded from
its portrait library artwork. Covers load through the existing background art
queue and persistent cache; missing artwork leaves the normal placeholder.
Use **Rename selected** in the Windows launcher to edit a game's display title.
Its ID, executable, arguments and approval remain unchanged. Reopen GAMEZ on
the TV to load the new title and its matching cover.

On the TV, Triangle/Y opens a rename keyboard for the selected approved game;
Start opens genre and sort controls. TV names are cosmetic overrides saved in
`gamez-names.json` by companion ID. They do not require a PC update or alter the
PC catalog, approval or launch configuration. The override also drives artwork
and metadata matching. Genre and sort choices persist across restarts.

Latest orders by Steam release date. Most Popular orders by Steam recommendation
totals. These public metadata fields refresh through the background artwork queue
and are cached for a day. Games without a metadata match remain available in
All Genres and Unknown. Recently Played records accepted FlXtR launch requests
on this device in `gamez-history.json`, keyed by companion ID so renames retain
history. Never-played and no-longer-approved games are excluded from that view.

Before creating the game process, the companion shows a black cover and stops
Explorer in the current user's Windows session. Owned game windows are raised
above that cover, including windows created after a bootstrap exits. Explorer
restarts are suppressed while the shell is hidden. A Windows Job Object tracks
the bootstrap and its normally inherited child processes. When they exit, the
Sunshine wait command completes, Moonlight disconnects, and FlXtR returns.
The undo command restores Explorer behind the cover during stream teardown.
The managed Moonlight connection also quits its Sunshine application when the
viewer disconnects, so backing out of a stream restores the Windows shell.

An ordinary Desktop stream keeps its existing behavior. The companion never
uses kill-on-job-close, so a companion crash does not terminate an unsaved game.
Launch failure and missing-viewer timeouts restore Explorer. Sunshine's undo
command has an independent delayed Explorer recovery if the companion has died.
For manual recovery, use Task Manager → Run new task → `explorer.exe`.

Processes launched indirectly by an already-running external launcher or service
may not inherit the game job. Those games need a launcher-specific completion
signal before they can use this session mode reliably.

The Windows startup task runs `Update-And-Start.ps1`. It checks the `gamez-updates`
release manifest, validates the package checksum, executes native lifecycle
self-tests, and retains the previous `app` directory as `app.previous`. Catalog,
approval records, and private connection tokens remain outside the package.
Candidate releases use immutable `gamez-COMMIT` tags and are tested before the
stable manifest is advanced.

The startup script also supervises the companion: an exit triggers a restart;
repeated failed health checks or a stalled UI trigger replacement of that exact
companion process. Slow launch verification gets up to three minutes before it
is treated as stalled. Rapid crash loops use a longer restart delay. A Windows
mutex and the task's IgnoreNew policy prevent duplicate supervisors.

Game window positioning uses asynchronous requests, and session/health reads do
not marshal through the Windows UI thread. Named game jobs are held by a small
helper for the game's lifetime. The companion persists the job name, game ID,
session ID and viewer state, then reconnects to that job after restart. Recovery
does not stop the scheduled task or terminate game processes. Native job tests
cover reopening an existing job and keeping a game alive after the launcher's
handle closes. `Test-Watchdog.ps1` runs the actual PowerShell supervisor against
isolated test children to exercise both exit and hung-UI restart paths.

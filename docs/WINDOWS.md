# Windows port — step 1

Status: **platform foundation, not a complete Windows application**.
The Windows executable produced here is a developer test harness. There is no
Windows GUI/player release yet. Linux and Steam Link builds remain available.

## Build directly in Debian/Ubuntu WSL

Run outside Docker:

```sh
sudo apt-get update
sudo apt-get install gcc-mingw-w64-x86-64-posix
bash scripts/test-platform.sh
bash scripts/build-windows.sh
```

Output: dist/FlXtR-Windows-platform-x86_64.zip and its SHA-256 file.
Extract it into a writable Windows folder and run in PowerShell:

```powershell
.\FlXtR-windows-platform-test.exe
```

The executable prints PASS messages and returns zero. It is not named FlXtR.exe
because the app itself is not yet ported. No MSYS2 installation is required on
the test machine; Windows system DLLs provide the Win32 and BCrypt APIs.

## Implemented

- UTF-8 argument vectors passed through CreateProcessW on Windows and
  posix_spawn on POSIX, without a command shell.
- Empty arguments, quotes, backslashes and Unicode survive Windows argv parsing.
- Explicit standard handle inheritance using STARTUPINFOEX; unrelated inherited
  handles/descriptors are excluded without changing parent descriptor flags.
- stdout/stderr file redirection, nonblocking polling, bounded waits, exit
  status, forced termination and process handle cleanup.
- Cryptographic random bytes using BCryptGenRandom or /dev/urandom.
- Nonblocking exclusive file locks and replacement of an existing file.
- Catalog requests, catalog prefetch, controller-idle and updater spawning on
  Linux now use the shared process code through a temporary POSIX adapter.
- Phone session tokens use the shared randomness helper.
- Windows/Linux runtime tests and a GitHub Actions cross-build/Windows test job.

The process API owns one direct child. Windows stop currently uses
TerminateProcess; graceful app shutdown and process-tree management are pending.
The existing shell uses /bin/sh for its Linux helpers; those call sites still
need Windows-native commands before the full shell can target Windows.

## Remaining for a usable Windows app

1. Migrate the remaining fork/exec/waitpid/signal users and their process IDs
   to FlxtrProcess, including player launch, artwork workers and game launching.
2. Port player control/preload pipes, phone server sockets and platform paths.
3. Remove Linux-specific utilities and ConnMan assumptions from Windows paths.
4. Build Windows SDL2, SDL2_image, curl, json-c, OpenSSL and libmpv dependencies;
   cross-compile the shell, catalog, resolver and player.
5. Add a native launcher, Windows updates, DLL packaging and media/controller tests.

MinGW-w64 does not make fork() or POSIX signals work on Windows. Installing the
compiler alone cannot build the unchanged Linux app. We keep shared C logic and
add native backends rather than adding a second UI implementation in C#.

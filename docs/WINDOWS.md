# Windows port — steps 1 and 2

Status: **native platform, IPC and phone-server backends; not a complete Windows app**.
The package contains developer test executables. There is no Windows GUI/player
release yet. Linux and Steam Link application builds remain available.

## Build directly in Debian/Ubuntu WSL

Run outside Docker:

```sh
sudo apt-get update
sudo apt-get install gcc-mingw-w64-x86-64-posix libmpv-dev
bash scripts/test-platform.sh
bash scripts/build-windows.sh
```

Output: dist/FlXtR-Windows-platform-x86_64.zip and its SHA-256 file.
Extract it into a writable Windows folder. In PowerShell:

```powershell
.\FlXtR-windows-platform-test.exe
.\FlXtR-windows-ipc-test.exe
```

Both executables must print PASS messages and return zero. No MSYS2 installation
is required on the Windows test machine. Neither executable is the media app.

The optional FlXtR-phone-server-test.exe runs the actual phone setup HTTP server
on **localhost only** and prints its temporary URL. The package includes its phone
HTML/JS assets. Ctrl+C stops the fixture. Files or keys intentionally saved through
its setup pages remain in the working folder; it does not change existing app data.
This fixture is not a LAN phone-remote release.

## Implemented

- UTF-8 process arguments through CreateProcessW/posix_spawn, redirection, exact
  quoting, bounded waits, exit status, termination and handle cleanup.
- Explicit handle/descriptor inheritance, extended in step 2 to export only
  selected player pipe endpoints without leaking unrelated handles.
- Private, local-only Windows byte pipes with nonblocking control writes/reads,
  backpressure and EOF; POSIX uses anonymous pipes. The player control/preload
  transport is tested across real parent/child processes on both operating systems.
- The shared desktop player accepts native Windows control/preload handles and
  existing POSIX descriptor arguments. Its real source cross-compiles to a Windows
  object with the public libmpv header. Windows linking/DLLs are still pending.
- The actual phone HTTP server uses Winsock startup/shutdown, socket-sized
  handles, nonblocking IO, closesocket and native adapter discovery on Windows.
  Linux retains POSIX sockets and interface discovery.
- Rejected HTTP requests half-close the response and drain unread input with a
  bound, preventing Windows TCP resets from discarding the HTTP error response.
- Cryptographic randomness via BCrypt/urandom, exclusive file locks and file
  replacement. Windows key/upload temporaries have a current-user-only DACL.
- Binary uploads, UTF-8 filesystem paths, no-overwrite publication, free-space
  checks, rejection of Windows device basenames, and no-follow local media checks.
- A native data-directory helper returns LOCALAPPDATA\\FlXtR on Windows and the
  XDG data path on Linux. Existing app data has not been relocated; the future
  Windows launcher will use this helper.
- Linux catalog/prefetch/controller/updater helpers use the shared spawn layer
  through a transitional POSIX adapter; phone session tokens use shared randomness.

The pipe implementation intentionally offers synchronous nonblocking polling, as
used by the existing player; it is not an overlapped-I/O API. A pipe write may be
partial or return EAGAIN, and callers must retain/retry bytes that were not written.
The process API owns one direct child. Windows stop currently uses
TerminateProcess; graceful app shutdown and process-tree management are pending.

## Validation

GitHub Actions cross-compiles in Linux and runs both test executables plus the
same HTTP integration suite against the real server on Windows. Tests cover
spaces/Unicode, argument quoting, handle isolation, control/preload messages,
empty/full/disconnected pipes, repeated cleanup, private file ACLs, binary uploads,
duplicate/traversal/device-name rejection, key replacement, fragmented HTTP,
origin/host rejection and setup expiry.

Linux regression checks include the shared platform tests, real desktop player
playback/control/preload tests, existing helper spawning, phone state and HTTP.
ASan/UBSan checks run locally for the IPC/platform layer.

## Remaining for a usable Windows app

1. Migrate remaining fork/exec/waitpid/signal users and process IDs to
   FlxtrProcess: full UI/player launch, artwork workers, music and game launching.
   Linux /bin/sh helpers still need native Windows replacements.
2. Build/link Windows SDL2, SDL2_image, curl, json-c, OpenSSL and libmpv, then build
   the full shell/catalog/resolver/player executable set.
3. Add the native launcher, Windows updates and DLL packaging; integrate the
   data-directory helper and validate interactive playback and controllers.

MinGW-w64 does not make fork() or POSIX signals work on Windows. Installing the
compiler alone cannot build the unchanged Linux app. Shared C logic and native
backends are retained rather than introducing a second C# UI.

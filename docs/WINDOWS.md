# Windows application (experimental)

The native x86-64 Windows package contains **FlXtR.exe**, the SDL UI, catalog
worker, WASM-backed resolver, libmpv video/audio players, phone HTTP setup,
and a private Python/yt-dlp/EJS/QuickJS music runtime. It runs without WSL,
Docker, MSYS2, a Python installation, or C# on the user's Windows machine.

## Run

Extract the entire FlXtR-Windows-x86_64.zip into a folder and open FlXtR.exe.
Keep its DLLs, assets, certs, tools and music-runtime folders together.
Use a controller or keyboard: arrows, Enter/A, Escape/B.

Settings, keys, favorites and caches are stored under %LOCALAPPDATA%\FlXtR.
The optional FLXTR_DATA_DIR environment variable selects another existing or
creatable directory. Package resources are found relative to the executable,
so shortcuts and launching from another working directory work.

This is a first experimental Windows application, not a stable release.
Provider availability and controller/device compatibility need more user testing.
The Windows update action opens GitHub releases; extract a new Windows ZIP
to update. Steam Link's ConnMan Wi-Fi provisioning and controller sleep helper
are not used on Windows. RetroArch, ScummVM, ROMs and libretro cores are not bundled.
For RetroArch set FLXTR_RETROARCH_HOME to the installed directory containing
retroarch.exe, cores, and roms; Windows cores end in _libretro.dll.
ScummVM can be installed on PATH.

## Build in regular Debian/Ubuntu WSL

Run **outside Docker**, from this branch's repository checkout:

    sudo apt-get update
    sudo apt-get install -y build-essential cmake python3 zstd wget
    python3 scripts/build-windows-app.py

The script downloads a checksum-pinned LLVM-MinGW UCRT compiler and MSYS2 UCRT
headers, import libraries and runtime packages into build/windows-sdk.
scripts/windows-sdk.lock.json pins every URL, version and SHA-256.
The existing resolver script verifies and prepares its WABT/decoder inputs.
No MSYS shell or Windows-side build tool is needed.

Output:

- dist/FlXtR-Windows-x86_64.zip — complete application and DLLs.
- dist/FlXtR-Windows-x86_64.zip.sha256.
- build/windows-app/windows-app-test.exe — developer integration helper.

For subsequent offline builds with the SDK already present:

    python3 scripts/build-windows-app.py --no-fetch

The resolver's verified source/build cache must also exist for an offline build.
GREENLINK_BUILD_CACHE optionally selects it.

## Validation

The Windows CI job runs the complete package from a path containing spaces and
Unicode. tests/windows_app_test.py checks the rendered SDL UI, actual UI-to-player
launch, native libmpv playback, inherited control/preload pipes, catalog resources,
Unicode JSON/ZIP paths, bounded ZIP extraction/traversal rejection, Python TLS,
yt-dlp/EJS, QuickJS and relocated music-worker options. Network providers are
excluded from the deterministic CI tests.

The preceding foundation tests remain separately available:

    sudo apt-get install -y gcc-mingw-w64-x86-64-posix libmpv-dev
    bash scripts/test-platform.sh
    bash scripts/build-windows.sh

FlXtR-Windows-platform-x86_64.zip contains developer validation executables;
it is separate from the complete application ZIP. Its tests cover process
quoting, Unicode arguments, strict handle inheritance, waits/termination,
cryptographic randomness, locks, private files, backpressure, EOF and actual
phone HTTP setup/upload behavior. Linux build and integration regressions run
alongside the native Windows tests.

## Implementation and dependencies

Windows starts explicit argument vectors with CreateProcessW; it does not
emulate fork or execute app helpers through a command shell. Job Objects
contain worker trees. Native private local byte pipes carry player controls.
Wide filesystem, environment, directory and process APIs preserve UTF-8 paths.
ZIP enumeration/extraction uses libarchive; BIOS manifests use OpenSSL hashing.
Winsock implements the phone HTTP server.

The package includes upstream license files, source-package URLs and pinned
versions in third-party-licenses and THIRD-PARTY-SOURCES.json.
The dependency bundler recursively resolves PE imports and refuses unresolved
non-system DLLs. Python's runtime has its own DLL closure.

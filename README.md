# FlXtR-Steamlink

A tiny native movie-library shell (currently named Greenlink): black background, green pixel text, poster art,
and a sparse white starfield inspired by old emulator menus.

This is an **early prototype**, not a completed Flixer client. It does not bundle
Flixer's website or automatically resolve Flixer titles yet. The catalog accepts
direct HTTP(S) video URLs. No browser, Node, Python, or Docker runs on the box.
WSL/Docker are build tools on the PC.

![Native shell](preview.png)

## Footprint

- C99 + the Steam Link's existing SDL2 library; built-in 5x7 pixel font.
- Six cached poster textures, each at most 192x288. BMP files only; pre-size posters
  before deployment. Decoded poster texture pixels total at most 1.27 MiB.
- Fixed 128-title catalog; no background networking from the shell.
- 56 white stars, capped to approximately 30 updates/sec. Toggle them off to draw
  only when state changes. Actual total RSS includes SDL and graphics driver memory.
- The separate player uses FFmpeg libraries for HLS/MP4 demuxing and audio, and
  Valve **SLVideo** for hardware H.264 decoding. It selects normal mode for B-frames.

## Controls

| Action | Controller | Keyboard |
|---|---|---|
| Move | D-pad | Arrow keys |
| Play | A | Enter |
| Stop / back / exit | B | Escape |
| All / playable filter | X | Tab |
| Toggle stars | Y | Y |
| About | Start | I |

The player currently supports sequential playback and stop. Seeking, pause,
subtitles, network retry, and a robust audio-master synchronization loop remain work
in progress. Do not mistake a successful frame-submission test for verified lip sync.

## Build the shell

Run in a WSL Linux shell with Docker access:

```sh
docker build -t greenlink-sdk .
docker run --rm -v "$PWD:/src" greenlink-sdk
```

An existing image with Valve's SDK at `/opt/steamlink-sdk` also works:

```sh
docker run --rm -v "$PWD:/src" -w /src steamlink-sdk-wsl bash scripts/build-steamlink.sh
```

Output: `dist/greenlink.tgz` and `dist/steamlink/apps/greenlink/`.

## Build the player

```sh
docker run --rm -v "$PWD:/src" -w /src greenlink-sdk bash scripts/build-player.sh
docker run --rm -v "$PWD:/src" -w /src greenlink-sdk bash scripts/build-steamlink.sh
```

FFmpeg 4.4.5 is pinned for the old Valve SDK toolchain; download SHA-256 is checked.
The build enables a restricted set of codecs/protocols and uses the firmware's
GnuTLS library with certificate verification enabled. This legacy dependency needs
an update/security review before treating the application as production-ready.
Build-cache persistence is optional via `GREENLINK_BUILD_CACHE` and a Docker mount.
FFmpeg compilation uses four jobs by default; override `JOBS` if needed.

## Install

Copy the `dist/steamlink` folder onto a FAT32 USB drive, insert it into the Steam
Link, then power-cycle it. The Greenlink entry appears in the native menu. Existing
firmware and other apps are not replaced.

Alternatively, extract `dist/greenlink.tgz` under `/home/apps` over an authenticated
SSH session. Exit a running Moonlight/Steam session before testing video output.

## Catalog

`catalog.tsv` has four tab-separated fields per line:

```text
TITLE<TAB>METADATA<TAB>RELATIVE POSTER BMP PATH<TAB>DIRECT HTTP(S) VIDEO URL
```

An empty URL shows `SOURCE NEEDED`. The sample catalog contains original procedural
art and empty playback URLs, not a scraped movie collection. Add a working direct
URL before trying playback; automatic title lookup is not yet implemented.
Use `catalog.local.tsv` for private or temporary URLs (excluded from Git), and launch
`./greenlink --catalog catalog.local.tsv`. Playback URLs may expire.

## Desktop tests

Install a C compiler, pkg-config and SDL2 development files, then:

```sh
make test
./build/greenlink
./build/greenlink --no-stars
```

`--screenshot file.bmp` renders one frame; `--frames N` exits after N rendered
frames. `SDL_VIDEODRIVER=dummy` allows headless shell tests.

## Research and remaining work

See [RESEARCH.md](RESEARCH.md). In particular, a working URL outside a browser does
not prove a fully native, automatic Flixer resolver. No third-party resolver code,
passwords, or scraped playback links are included in this repository.

The intended experience is controller-first: title, season/episode for TV, server,
quality, then playback. This first prototype has the library grid, ready filter,
star toggle and direct playback launch. Server/quality and season/episode menus
are not implemented yet. The final application must operate with the PC off;
no PC-side resolver is part of the intended architecture.

## Licensing

Original Greenlink code and procedural artwork: MIT. Valve SDK/system libraries
retain their licenses. FFmpeg is configured without GPL/nonfree components and is
LGPL 2.1-or-later; static binary redistribution requires its corresponding source,
license notices and relinkable application object files. Build scripts download
FFmpeg from its official upstream. Do not publish player binaries without those
materials; source-only publication is separate from binary distribution.

# FlXtR-Steamlink

A tiny native movie-library shell: black background, green pixel text, poster art,
and a sparse white starfield inspired by old emulator menus.

This is an **early prototype**, not a completed Flixer client. Native movie/series
browsing, search, seasons and episodes now work on the box. Automatic Flixer
playback-link resolution is still missing: a listing is not a playable source.
Direct HTTP(S) URLs and multiple locally supplied server/quality entries work.
No browser, Node, Python, or Docker runs on the box. WSL/Docker are build tools.

![Native shell](preview.png)

## Footprint

- C99 + the Steam Link's existing SDL2 library; built-in 5x7 pixel font.
- Six cached poster textures, each at most 192x288. BMP files only; pre-size posters
  before deployment. Decoded poster texture pixels total at most 1.27 MiB.
- At most six entries per online page; local direct-URL catalogs accept 128 rows.
- A separate short-lived C worker fetches metadata and six bounded JPEG posters,
  using the firmware's curl, json-c and SDL2_image. The library stays responsive;
  B cancels loading. A large metadata snapshot is scanned from disk, not kept in RAM.
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
| Local / movies / series | X | F2 |
| Search movies or series | Start | / or F3 |
| Type in search | D-pad + A | Keyboard |
| Submit search | Start or the > key | Enter |
| Delete search character | B or the < key | Backspace |
| Next / previous catalog page | Move beyond grid edge | Page Down / Page Up |
| All / playable filter in local catalog | — | Tab |
| Toggle stars | Y | Y |
| About outside movie/series root | Start | I |
| Select viewing mode in library | LB / RB | V |
| Cycle viewing mode during playback | Y / LB / RB / Start | Controller required |

Launching a movie releases the library's graphics layer completely. Stopping it
recreates the library at the same selection. Viewing modes are fullscreen fit
(preserve aspect ratio), stretch (fill display), and centered 1:1 source pixels.
1:1 is unavailable when the source is larger than the display. A small mode label
appears for 2.5 seconds after switching, then disappears.

Viewing modes use an optional Marvell viewport adapter because SLVideo has no
public video-rectangle API. It captures the live handle from SLVideo's existing
viewport call, verifies rectangle readback, and restores the original viewport on
exit. It uses no private object offsets. This is experimental: rectangle acceptance
does not establish visual scaling on every firmware. Unsupported modes report
`VIEW MODE UNAVAILABLE`. Keyboard events during playback are unavailable while
the library's video subsystem is released; controller input remains active.

The player currently supports sequential playback and stop. Seeking, pause,
subtitles, network retry, and a robust audio-master synchronization loop remain work
in progress. Do not mistake a successful frame-submission test for verified lip sync.

Press X to choose Series, A on a show, then A on a season and episode. B retraces
those screens and restores the previous selection. Source choices show a compact
server/quality list. Missing links display an explicit message; selecting a title
never silently substitutes a different episode or a demo video.

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

### One-command rebuild inside Docker

The Dockerfile installs `/usr/local/bin/autocompile`. To add it to an existing SDK
container, copy `scripts/autocompile` there and run `chmod 755` on that file.

```sh
autocompile               # update main, compile the player and shell, package
autocompile --shell-only  # just rebuild the shell (retains an already-built player)
autocompile --no-update   # build local changes without fetching
```

The default checkout is `/opt/FlXtR-Steamlink`; output is
`/opt/FlXtR-Steamlink/dist/greenlink.tgz`. FFmpeg is cached in
`/var/cache/flxtr-steamlink`. Updates refuse to overwrite local changes.
This command builds only; it stores no SSH credentials and does not interrupt the box.

## Build the player

```sh
docker run --rm -v "$PWD:/src" -w /src greenlink-sdk bash scripts/build-player.sh
docker run --rm -v "$PWD:/src" -w /src greenlink-sdk bash scripts/build-steamlink.sh
```

FFmpeg 4.4.5 is pinned for the old Valve SDK toolchain; download SHA-256 is checked.
The build enables a restricted set of codecs/protocols and uses the firmware's
GnuTLS library with certificate verification enabled. The package includes a
SHA-256-pinned Mozilla CA bundle from curl (2026-09-25, MPL 2.0) because the old
firmware certificate store cannot validate the current poster CDN. The app uses
its own bundle; it does not modify system trust or disable verification.
This legacy dependency needs
an update/security review before treating the application as production-ready.
Build-cache persistence is optional via `GREENLINK_BUILD_CACHE` and a Docker mount.
FFmpeg compilation uses four jobs by default; override `JOBS` if needed.

## Install

Copy the `dist/steamlink` folder onto a FAT32 USB drive, insert it into the Steam
Link, then power-cycle it. The FlXtR Steamlink entry appears in the native menu. Existing
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
The menu launcher automatically uses `catalog.local.tsv` when it exists. This
private catalog is excluded from Git and should be preserved during updates.

### Movie and series listings

The native worker talks directly to the site's public metadata API. With no
snapshot installed it fetches popular listings on demand. Search always queries
the API; season and episode details also load on demand, including season 0.

To cache both popular lists on disk, run on your build machine:

```sh
python3 tools/crawl_catalog.py --cache /tmp/flxtr-metadata --output library.local.tsv
```

Copy `library.local.tsv` into the installed app directory. The crawler resumes
cached pages, deduplicates IDs, and issues at most two requests per second. It
collects metadata only. The upstream endpoint caps each list at 500 pages even
though it reports more pages; this is not a complete index of every playable title.
Search can find entries outside those popular lists. Metadata and artwork are
provided by TMDB through the site's metadata service; FlXtR is not endorsed or
certified by TMDB.

### Multiple sources

Private `sources.local.tsv` rows have seven tab-separated fields:

```text
TMDB_ID<TAB>SEASON<TAB>EPISODE<TAB>movie|tv<TAB>SERVER LABEL<TAB>QUALITY LABEL<TAB>DIRECT URL
```

Movies use season/episode `0 / 0`; TV uses actual season and episode numbers.
Multiple rows provide selectable servers or quality variants for that exact title.
These are supplied URLs, not automatically extracted servers or HLS renditions.
The verified local test link remains separate from this public repository.
`catalog.local.tsv`, `library.local.tsv`, `sources.local.tsv` and `catalog-cache/`
are ignored by Git and preserved across package updates.

## Desktop tests

Install a C compiler, Python 3, pkg-config, and SDL2, SDL2_image, curl and json-c
development files, then:

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

The controller flow is title, season/episode for TV, source/quality, then playback.
The screens are implemented; automatic server lookup is the remaining gap between
catalog browsing and playback of arbitrary titles. The application operates with
the PC off; no PC-side resolver is part of the architecture.

## Licensing

Original Greenlink code and procedural artwork: MIT. Valve SDK/system libraries
retain their licenses. FFmpeg is configured without GPL/nonfree components and is
LGPL 2.1-or-later; static binary redistribution requires its corresponding source,
license notices and relinkable application object files. Build scripts download
FFmpeg from its official upstream. Do not publish player binaries without those
materials; source-only publication is separate from binary distribution.

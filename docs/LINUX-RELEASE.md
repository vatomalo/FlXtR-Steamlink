Native Linux beta: the existing coverflow/library UI, native catalog and source
resolver, plus a libmpv desktop player. Includes KissAnime, Internet Archive,
movies/series, TV mode and controller playback controls. No Steam Link or PC-side
streaming service is needed.

Download `FlXtR-linux-x86_64-install.tgz`, extract it into a writable directory,
and run `./FlXtR.sh` from inside the extracted folder. The other `.tgz` is an
updater payload, not a standalone installation.

From the source checkout you can instead run
`bash scripts/autobuild --download --install`. It downloads and verifies the
executable package, installs it under your user account, and uses sudo only to
add `/usr/local/bin/flxtr`. Run the script as your normal user; it prompts for sudo
when needed. `--build --install` builds locally; `--deps` installs dependencies.

This binary was built on Debian 13 x86_64. Install runtime dependencies:
`sudo apt install libsdl2-2.0-0 libsdl2-image-2.0-0 libcurl4t64 libjson-c5 libssl3t64 libmpv2 python3 ca-certificates`.
For another distribution or ARM64, build from source using `docs/LINUX.md`.

Startup checks the separate Linux beta channel. Only newer builds for the same
architecture are installed, with checksums, startup checks and a retained previous
release. Settings and caches live in `data/`. Create `data/.no-auto-update` to
disable startup checks. Steam Link releases are unaffected.

Keyboard: arrows/Enter to navigate, Escape to go back, X to change library,
F5 for settings. During video, Tab opens the playback menu and Escape closes it
or stops playback. Controllers retain the existing console controls.

Beta limits: buffering is bounded RAM on Linux, rather than the Steam Link disk
ring. Quality selection for HLS is bitrate-based; unusual provider ladders may
differ from the selected height. PSP streaming is not included in this release.

Validation: native Linux build; local H.264/AAC playback; live KissAnime 720p video
and stereo audio decoded with headless output; update rejection and rollback tests.
Interactive desktop playback and other distributions still need user testing.

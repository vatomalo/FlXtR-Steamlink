# Linux beta

Download and install the prebuilt beta, without compiling:

```sh
bash scripts/autobuild --download
bash scripts/autobuild --download --install
```

Run as your normal user. `--install` verifies the package, installs the app under
`~/.local/share/flxtr`, then invokes `sudo install` for `/usr/local/bin/flxtr`.
Launch it with `flxtr`. Automatic updates remain user-owned and never prompt for
sudo. Existing settings are preserved. `autocompile --linux --install` is an alias.
The original Steam Link `autocompile` behavior is unchanged without Linux flags.

To compile and leave the executable in `/build`:

```sh
bash scripts/autocompile --linux
/build/FlXtR.sh
```

The native ELF is `/build/greenlink`; matching player/catalog/resolver executables
are also placed there. Launch with `FlXtR.sh` to set up data, helpers and updates.
Linux mode now compiles by default; use `--download` for download-only mode.
If `/build` does not exist and requires root permission, the script uses sudo to
create it owned by your account. Set `FLXTR_LINUX_OUTPUT_DIR` for another location.
Root builds inside Docker are allowed; `--install` must run as a normal user.

Use `--deps` to install Debian/Ubuntu dependencies with sudo, and `--build --install`
to compile locally instead of downloading. `FLXTR_INSTALL_DIR` and
`FLXTR_DOWNLOAD_DIR` override the app and download locations. A system-wide launcher
points to the installing user's app; this is not a shared multi-user installation.

Build on the Linux machine that will run it:

```sh
sudo apt install build-essential cmake pkg-config wget python3 git \
  libsdl2-dev libsdl2-image-dev libcurl4-openssl-dev libjson-c-dev libssl-dev libmpv-dev
git clone https://github.com/vatomalo/FlXtR-Steamlink.git
cd FlXtR-Steamlink
bash scripts/build-linux.sh
./dist/FlXtR-linux-$(uname -m)/FlXtR.sh
```

The first resolver build downloads pinned WABT and the upstream decoder module.
It runs entirely locally after building. The source checkout and Linux package
are independent of the Steam Link installation. Private TSV catalogs can be copied
into the Linux package's `data/` directory.

The Linux backend uses libmpv for video/audio, subtitles and hardware decoding
where supported, with software fallback. It accepts the shell's existing player
arguments and control pipe. Seeking and source/quality changes preserve the shell's
restart contract, including refreshing expiring MegaPlay URLs. Tab/Options opens
an in-video menu. Keyboard events belong to the player window; controller events
continue to come from the shell.

Linux buffers in RAM (bounded by the selected buffer size). mpv's disk cache is
append-only, so it is deliberately not substituted for the Steam Link's bounded
ring file. The HLS bitrate target approximates the 480/720/1080 settings. Direct
files are decoded at their native resolution and scaled by the desktop player.

The updater uses `linux-<architecture>-latest`, separate from Steam Link's latest
release. It compares numeric build times, checks architecture and SHA-256,
validates archive paths and file types, and executes `--version` on the proposed
shell/player before switching a `current` symlink atomically. An interrupted
download leaves the old installation usable. `previous` retains the prior release.
No user data is part of the downloaded payload.

Publish from a clean checkout after rebuilding:

```sh
bash scripts/publish-linux.sh
```

Requires an authenticated GitHub CLI. All Linux releases are marked prerelease;
they do not change the Steam Link's stable/latest channel.

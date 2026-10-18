# Linux and Steam Deck packages

Build both x86_64 editions in WSL with Docker:

```sh
bash scripts/build-desktop.sh
```

Outputs are dist/FlXtR-Linux-x86_64.tgz and
dist/FlXtR-SteamDeck-x86_64.tgz, each with a SHA-256 file.
The builder uses Ubuntu 22.04 (glibc 2.35). Application shared libraries and their
distribution copyright files are included. glibc and GPU driver libraries come
from the host. These packages do not require the webOS or Steam Link SDK.

Extract into a writable directory. On Linux, run FlXtR.sh.
On Steam Deck, use Desktop Mode to add FlXtR-SteamDeck.sh as a non-Steam game,
leave Proton disabled, and select the Gamepad controller layout. That launcher
selects X11 for the Gamescope session. Settings are stored under data/.
Do not run the app as root or change the SteamOS read-only filesystem.

These bundled editions disable startup updates using data/.no-auto-update.
The existing Linux updater channel provides unbundled binaries, so upgrades for
these editions are manual: extract the next release and copy data/ across.

Requires a display/audio session, Python 3, system CA certificates and functioning
system graphics drivers. Steam Deck hardware and interactive playback need user
validation; container startup checks do not establish hardware compatibility.

To repeat a build, move existing dist/FlXtR-Linux-x86_64 and
dist/FlXtR-SteamDeck-x86_64 directories aside first.

Publish both packages from a clean, committed checkout after building:

```sh
bash scripts/publish-desktop.sh
```

Push the build commit to GitHub before publishing. The script verifies checksums
and embedded revisions and publishes a prerelease without changing updater channels.

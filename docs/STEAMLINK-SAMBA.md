# Steam Link local sharing (experimental)

The optional Samba add-on exposes app-local `roms` and `videos` folders as
password-protected ROMs and Movies shares. Existing RetroArch ROMs are also
shared when present. NAS/PC shares and NFS are deferred.

## Install from Settings

Open Settings > Services > SAMBA DOWNLOAD and press A/Enter. The status line
reports download progress. Select SAMBA PASSWORD, choose 8-64 characters using a
keyboard/controller, and press Start/Enter. Saving configures and enables
sharing. Username is root; your SSH password stays unchanged. SAMBA SHARING
toggles sharing and startup on/off.

Password keyboard: A types, X changes case, Start saves and B cancels. Keyboard
Backspace deletes. Passwords are masked and sent through an anonymous pipe,
never command-line arguments or saved input files. Only Samba's private
password database persists.

The installer downloads 53 pinned Alpine ARMv7 packages over verified HTTPS
and checks SHA256 and size against samba-packages.json. It unpacks into a
temporary app-local directory with path/link/size checks, then installs by a
single rename after ARM architecture and completeness checks. Failed downloads
keep existing installations. Allow at least 160 MiB free storage; download is
about 20 MiB, installed runtime about 48 MiB. This service is Steam Link only.

No GitHub Samba binary release is needed: runtime packages come directly from
Alpine. PACKAGE-SOURCES.json retains each package's license, source origin and
aports commit. The small path-relocation library ships with FlXtR. The manifest
was generated from apk fetch --recursive against Alpine's signed 3.23 index,
using the pinned images/ARM keys from build-samba-addon.sh. If upstream removes
a pinned package, install fails safely; update FlXtR to obtain refreshed pins.

## Manual alternative

Build from regular WSL/Linux:

    bash scripts/build-steamlink-package.sh
    bash scripts/build-samba-addon.sh

Extract both archives under `/home/apps` on the Steam Link, preserving existing
files. In `/home/apps/greenlink`, run `sh samba-server.sh configure` interactively
to choose an SMB password for username root, then `sh samba-server.sh enable`.
The SSH password is unchanged. Use Windows Explorer at `\\192.168.10.2\ROMs`
or `\\192.168.10.2\Movies`, or Linux `smb://192.168.10.2/ROMs`.
Sharing the movie folder does not add a movie-library screen to FlXtR.

`status`, `check`, `stop`, `enable` and `disable` are supported. Enable starts
sharing and enables startup with FlXtR. Disable stops it and disables startup.
The regular updater does not install the optional runtime. Default is off.

SMB1 and guests are disabled, signing is required, and symlinks are not followed.
Password databases and logs stay outside the shares in `greenlink/samba`.
Only this app's Samba instance is stopped. Use a trusted LAN, without forwarding
port 445. The server uses the device root identity restricted to explicit shares;
it is not a sandbox against trusted authenticated users.

## Runtime and tests

Signed Alpine 3.23 ARMv7 APKs provide Samba 4.22.10-r0 and dependencies. Pinned
Docker images provide download tools and keys. PACKAGES.txt records exact
versions; APK-PROVENANCE.txt records licenses, origins and aports commits.
Samba is GPL-3.0-or-later. Source is at
https://github.com/samba-team/samba/tree/samba-4.22.10 and packaging recipes and
patches at https://gitlab.alpinelinux.org/alpine/aports at the recorded commits.
Before public binary redistribution, provide corresponding sources, build
recipes and applicable license texts alongside the runtime.

An app-local musl loader and src/samba_paths.c relocate Samba modules/helpers.
Firmware libraries, system config and passwords are not replaced. Tests run
the actual ARM binaries under QEMU, covering authenticated SMB3 transfers,
share listing, denied guests, symlink confinement and lifecycle. Hardware
and network testing on the Steam Link remains necessary.

## Games

Settings > GAME OUTPUT selects Native, 240p (320x240), 480p (640x480), 720p or 1080p.
Steam Link defaults to 480p; desktop defaults to Native. Only the generated
per-launch RetroArch output config changes. Core native rendering and global
RetroArch settings remain unchanged. Display mode support depends on driver
and display.

## ScummVM reset

Close ScummVM, then run sh reset-scummvm-config.sh in the installed greenlink
folder. This moves scummvm.ini and .scummvmrc from the ScummVM app tree and
current home config locations into a timestamped backup next to that app.
Games and saves are not removed. ScummVM starts with defaults and may need its
game folders added again. The script reports every config moved.

The Steam Link ROM-menu launcher now uses ScummVMs own .home and app directory.
The reset also covers legacy configs under greenlink/.home and greenlink/.config.
Run the reset only while ScummVM is closed; it is not run by package extraction.

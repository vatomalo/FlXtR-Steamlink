# Steam Link local sharing (experimental)

The optional Samba add-on exposes app-local `roms` and `videos` folders as
password-protected ROMs and Movies shares. Existing RetroArch ROMs are also
shared when present. NAS/PC shares and NFS are deferred.

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

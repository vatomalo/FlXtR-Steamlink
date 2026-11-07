# SkyLoft builds

The private build page is at **http://100.69.7.81/** while connected to Tailscale.
Apache serves `/var/www/html/index.html`; the build API runs on loopback port
8787 as `flxtr-build-portal.service`. The page and API allow tailnet/loopback
addresses only. No separate web account is required.

Choose Android, Steam Link, Linux, Windows, the desktop pair, or all platforms,
then a branch, Android architecture, release
title, and Markdown change notes. Jobs run in the background, with persistent
history, live logs, per-build downloads, and GitHub release links. Closing the
browser does not stop a build. One build runs at a time, including CLI builds.

After connecting to SkyLoft over SSH, run from any directory:

```sh
flxtr-build --android
flxtr-build --steamlink
flxtr-build --linux --no-publish
flxtr-build --windows --no-publish
flxtr-build --desktop --no-publish
flxtr-build --android --branch codex/send-to-tv-settings
flxtr-build --all --branch codex/send-to-tv-settings
flxtr-build --android --no-publish
flxtr-build --android --arch arm64 --release-title "Phone improvements" --notes-file changes.md
```

`--branch NAME` selects and pulls that branch from `origin`. Without it, the
command pulls the currently selected branch. Updates use fast-forward only;
local changes and unpushed/diverging commits stop the command without resetting
anything. The selected branch remains checked out after the build.
Server runners are also retained outside the checkout so branches that predate
this command can use them, provided they have compatible platform build scripts.

All targets share a lock and run sequentially with `--all`. `--desktop` builds
Linux and Windows together. Desktop builds support x86-64; each includes the
complete application and its packaged dependencies. Linux requires the normal
host graphics drivers, glibc 2.35+ and Python 3.10+; Windows bundles its Python
runtime and DLL dependencies. No build tools are needed on the receiving device.
Android builds use the existing signing key and select a version code newer
than both the published channel and the previous local build.

Successful builds publish by default to Android's startup-update channel or
Steam Link's latest package release. Add `--no-publish` to keep artifacts local.
With `--all`, both builds must succeed before publication starts. A publishing
failure returns an error and leaves the built files available for inspection.
Publishing requires SkyLoft's own `gh auth login`; access is checked before
building. Rebuilding an already published Steam Link commit keeps its existing
release rather than overwriting it.
Custom titles and change notes are applied to GitHub releases. Android `--arch`
accepts `universal`, `arm64`, or `armv7`; Steam Link always uses ARMv7. Single-ABI
Android builds publish to their own release download, leaving the universal
auto-update channel unchanged. Choose Universal for the normal Android rollout.

Artifacts are in `/opt/dockers/storage/projects/FlXtR/source/dist/`:

- Android: `FlXtR-Android-preview.apk`
- Steam Link: `greenlink.tgz`
- Linux: `FlXtR-Linux-x86_64.tgz` (extract, run `FlXtR.sh`)
- Windows: `FlXtR-Windows-x86_64.zip` (extract, run `FlXtR.exe`)

All of these appear as per-build downloads on the website, even when Publish
is unchecked. Desktop publication creates a GitHub release with the finished
archive, checksum, title, and change notes. It does not replace the Steam Link
update channel. `--no-publish` does not require GitHub publishing credentials.

Logs are in `/opt/dockers/storage/build-logs/flxtr-*.log`. The command displays
the current log path when it starts. Android signatures are verified before
publication. Builds do not force installation on devices; their normal updater
behavior applies after publication.

Builds run in the foreground. To let one continue after disconnecting SSH:

```sh
nohup flxtr-build --android --branch codex/send-to-tv-settings > /opt/dockers/storage/build-logs/phone-build.log 2>&1 < /dev/null &
```

The installed command is `/usr/local/bin/flxtr-build`, copied from
`scripts/flxtr-build`. SkyLoft's existing Docker images, SDK cache, signing key,
and project configuration are required; this is not a new-server installer.

Install/update the page and service as root on SkyLoft with
`bash scripts/install-skyloft-build-portal.sh` after pulling `main`. This also
refreshes the installed build command and runners. The installer backs up the
previous web page under `/opt/dockers/storage/build-portal/backups/`.

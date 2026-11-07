# SkyLoft builds

After connecting to SkyLoft over SSH, run from any directory:

```sh
flxtr-build --android
flxtr-build --steamlink
flxtr-build --android --branch codex/send-to-tv-settings
flxtr-build --all --branch codex/send-to-tv-settings
```

`--branch NAME` selects and pulls that branch from `origin`. Without it, the
command pulls the currently selected branch. Updates use fast-forward only;
local changes and unpushed/diverging commits stop the command without resetting
anything. The selected branch remains checked out after the build.
Server runners are also retained outside the checkout so branches that predate
this command can use them, provided they have compatible platform build scripts.

Android and Steam Link builds share a lock and run sequentially with `--all`.
Android builds use the existing signing key and select a version code newer
than both the published channel and the previous local build.

Artifacts are in `/opt/dockers/storage/projects/FlXtR/source/dist/`:

- Android: `FlXtR-Android-preview.apk`
- Steam Link: `greenlink.tgz`

Logs are in `/opt/dockers/storage/build-logs/flxtr-*.log`. The command displays
the current log path when it starts. It builds and verifies the APK signature;
it does not publish releases or install apps on devices.

Builds run in the foreground. To let one continue after disconnecting SSH:

```sh
nohup flxtr-build --android --branch codex/send-to-tv-settings > /opt/dockers/storage/build-logs/phone-build.log 2>&1 < /dev/null &
```

The installed command is `/usr/local/bin/flxtr-build`, copied from
`scripts/flxtr-build`. SkyLoft's existing Docker images, SDK cache, signing key,
and project configuration are required; this is not a new-server installer.

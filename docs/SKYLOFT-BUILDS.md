# SkyLoft builds

After connecting to SkyLoft over SSH, run from any directory:

```sh
flxtr-build --android
flxtr-build --steamlink
flxtr-build --android --branch codex/send-to-tv-settings
flxtr-build --all --branch codex/send-to-tv-settings
flxtr-build --android --no-publish
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

Successful builds publish by default to Android's startup-update channel or
Steam Link's latest package release. Add `--no-publish` to keep artifacts local.
With `--all`, both builds must succeed before publication starts. A publishing
failure returns an error and leaves the built files available for inspection.
Publishing requires SkyLoft's own `gh auth login`; access is checked before
building. Rebuilding an already published Steam Link commit keeps its existing
release rather than overwriting it.

Artifacts are in `/opt/dockers/storage/projects/FlXtR/source/dist/`:

- Android: `FlXtR-Android-preview.apk`
- Steam Link: `greenlink.tgz`

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

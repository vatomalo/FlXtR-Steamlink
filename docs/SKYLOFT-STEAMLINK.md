# SkyLoft Steam Link workflow

The manual workflow builds the Steam Link app and experimental ARMv7 proxy
from the selected main commit. It runs regression tests before building and
prints the source SHA and checksums. Everything produced from the ROM stays on
SkyLoft or on the owner's Steam Link. Build output is never uploaded as a
GitHub Actions artifact.

## Runner setup

The repository is public. Keep pull-request jobs on GitHub-hosted runners and
require approval for all outside collaborators' fork workflows. Limit the
deployment runner to trusted workflow code. Where workflow-scoped runner
groups are available, allow only the deployment workflow at
`refs/heads/main`. If the runner cannot be isolated from untrusted jobs, leave
the deployment workflow disabled.

A SkyLoft administrator needs to provision a dedicated account for the runner,
with no sudo or Docker group membership and with a runner-owned SDK copy.
Valve's SDK setup writes its own Qt and libtool configuration. Use the
repository's **Settings → Actions → Runners → New self-hosted runner** flow to
obtain a short-lived registration token and the verified Linux x64 runner
archive. Register the label `skyloft-flxtr` and install the runner service for
the dedicated account. Keep registration material out of shell history and
job logs.

Configure a GitHub environment named `skyloft-steamlink` with deployment
restricted to `main`. Add reviewer approval where supported. Set its
`STEAMLINK_SDK_PATH`, `SOM_ROM_PATH` and `STEAMLINK_HOST` variables.
Give the runner only the SSH access needed for this device. Pin and verify
the Steam Link's SSH host key before its first job. Set the repository variable
`SKYLOFT_STEAMLINK_ENABLED` to `true` once the runner and environment are ready.

## Operations

- **build** compiles the app and proxy and generates the private packs.
- **deploy-proxy** requires RetroArch to be closed. It snapshots the installation,
  stages and verifies each experimental file and renames it into place.
  The install record includes the source SHA, installed hash and backup path.
- **rollback** restores experimental files from the deployment log. Close
  RetroArch, then launch the game again after rollback.

The application package is currently staged for local inspection. Deployment
covers the experimental proxy and its packs. To confirm which build is
running after installation, launch the game and compare the proxy path and
inode in the RetroArch process maps against the installed file. Compare the
installed SHA-256 against the record.

#!/bin/sh
# Public shell-only releases. Catalogs, player and resolver are never replaced.
set -eu
cd "$(dirname "$0")"
umask 077
base=https://github.com/vatomalo/FlXtR-Steamlink/releases
mkdir .update-lock 2>/dev/null || { echo 'Another update is active (remove .update-lock if a power loss left it behind).'; exit 1; }
cleanup() { rm -f .update-manifest .update-executable .update-backup; rmdir .update-lock; }
trap cleanup 0
trap 'exit 1' 1 2 15
fetch() {
    # ulimit also bounds servers with missing/misleading Content-Length.
    (ulimit -f 2048; curl -fsSL --proto '=https' --proto-redir '=https' \
        --cacert certs/cacert.pem --connect-timeout 5 --max-time 25 \
        --max-redirs 5 "$1" -o "$2")
}
if ! fetch "$base/latest/download/shell-manifest.txt" .update-manifest; then
    echo 'Update check unavailable. Keeping installed shell.'; exit 1
fi
[ "$(wc -c < .update-manifest)" -le 256 ] || exit 1
version=$(sed -n 's/^version=//p' .update-manifest)
sha=$(sed -n 's/^sha256=//p' .update-manifest)
case "$version" in ''|*[!a-f0-9]*) exit 1;; esac
case "$sha" in ''|*[!a-f0-9]*) exit 1;; esac
[ "${#version}" -eq 40 ] && [ "${#sha}" -eq 64 ] || exit 1
installed=$(./greenlink --version 2>/dev/null || true)
if [ "$installed" = "$version" ]; then echo 'Already up to date.'; exit 0; fi
echo "Downloading shell $version"
fetch "$base/download/shell-$version/greenlink" .update-executable
echo "$sha  .update-executable" | sha256sum -c - || exit 1
[ "$(od -An -tx1 -N4 .update-executable | tr -d ' \n')" = 7f454c46 ] || exit 1
chmod 755 .update-executable
# Confirms the binary can start with this device's libraries before replacing it.
[ "$(./.update-executable --version)" = "$version" ] || exit 1
cp greenlink .update-backup
mv -f .update-backup greenlink.previous
mv -f .update-executable greenlink
sync
echo "Installed $version. Previous shell saved as greenlink.previous."
exit 0

#!/bin/sh
# One-time bootstrap for older installations whose package updater is stuck.
# Run from the installed greenlink directory while FlXtR is stopped:
# sh install-package-release.sh package-<40-character commit>
set -eu
tag=${1:-}
case "$tag" in package-*) ;; *) echo "Usage: $0 package-<commit-sha>" >&2; exit 2;; esac
sha=${tag#package-}
case "$sha" in *[!0123456789abcdef]*|'') echo "Invalid release tag" >&2;exit 2;; esac
[ "${#sha}" -eq 40 ] || { echo "Expected 40-digit commit SHA" >&2; exit 2; }
[ -f ./greenlink ] && [ -f ./greenlink.sh ] || { echo "Run from FlXtR installation directory" >&2;exit 2; }
base="https://github.com/vatomalo/FlXtR-Steamlink/releases/download/$tag"
[ -r certs/cacert.pem ] || { echo "Missing certs/cacert.pem" >&2;exit 1; }
tmp="./.package-install-$$"
mkdir "$tmp"
cleanup(){ rm -rf "$tmp"; }
trap cleanup 0
trap 'exit 1' 1 2 15
curl -fLsS --cacert certs/cacert.pem --proto '=https' --proto-redir '=https' --connect-timeout 10 --max-time 180 "$base/build-info.txt" -o "$tmp/build-info.txt"
curl -fLsS --cacert certs/cacert.pem --proto '=https' --proto-redir '=https' --connect-timeout 10 --max-time 300 "$base/greenlink.tgz" -o "$tmp/greenlink.tgz"
version=$(sed -n 's/^version=//p' "$tmp/build-info.txt")
digest=$(sed -n 's/^sha256=//p' "$tmp/build-info.txt")
[ "$version" = "$sha" ] || { echo "Manifest version mismatch" >&2; exit 1; }
case "$digest" in *[!0123456789abcdef]*|'') exit 1;; esac
[ "${#digest}" -eq 64 ] || exit 1
echo "$digest  $tmp/greenlink.tgz" | sha256sum -c -
tar -tzf "$tmp/greenlink.tgz" | while IFS= read -r name; do
  case "$name" in
    greenlink/*) case "$name" in *../*|*/../*|/*) echo "Unsafe archive entry" >&2;exit 1;; esac ;;
    *) echo "Unexpected archive entry: $name" >&2;exit 1;;
  esac
done
mkdir "$tmp/extract"
tar -xzf "$tmp/greenlink.tgz" -C "$tmp/extract"
[ -f "$tmp/extract/greenlink/greenlink" ] || exit 1
# Preserve user state; replace only packaged application executables and assets.
cp ./greenlink "$tmp/greenlink.previous"
for file in greenlink greenlink-catalog greenlink-player greenlink-resolver update.sh; do
  if [ -f "$tmp/extract/greenlink/$file" ]; then
    cp "$tmp/extract/greenlink/$file" "./$file.next"
    chmod 755 "./$file.next"
    mv -f "./$file.next" "./$file"
  fi
done
[ -d "$tmp/extract/greenlink/assets" ] && { mkdir -p assets; cp -R "$tmp/extract/greenlink/assets/." assets/; }
cp "$tmp/greenlink.previous" greenlink.previous
sync
echo "Installed FlXtR $sha. Preserved settings, games and local catalogs."
./greenlink --version

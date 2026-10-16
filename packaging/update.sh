#!/bin/sh
# Full-package releases update all packaged binaries and assets; shell releases remain supported.
set -eu
cd "$(dirname "$0")"
umask 077
base=https://github.com/vatomalo/FlXtR-Steamlink/releases
mkdir .update-lock 2>/dev/null || { echo 'Another update is active (remove .update-lock if a power loss left it behind).'; exit 1; }
cleanup() { rm -f .update-manifest .update-package-manifest .update-package .update-executable .update-backup .update-lock/package-sha256; rm -rf .update-lock/stage; rmdir .update-lock; }
trap cleanup 0
trap 'exit 1' 1 2 15
fetch() {
    # ulimit also bounds servers with missing/misleading Content-Length.
    (ulimit -f 2048; curl -fsSL --proto '=https' --proto-redir '=https' \
        --cacert certs/cacert.pem --connect-timeout 5 --max-time 25 \
        --max-redirs 5 "$1" -o "$2")
}
# New full-package channel, using a separately signed-off immutable release tag.
# A missing package release falls back to the original shell update channel.
if fetch "$base/latest/download/package-manifest.txt" .update-package-manifest; then
    [ "$(wc -c < .update-package-manifest)" -le 256 ] || exit 1
    package_version=$(sed -n 's/^version=//p' .update-package-manifest)
    package_sha=$(sed -n 's/^sha256=//p' .update-package-manifest)
    case "$package_version" in ''|*[!a-f0-9]*) exit 1;; esac
    case "$package_sha" in ''|*[!a-f0-9]*) exit 1;; esac
    [ "${#package_version}" -eq 40 ] && [ "${#package_sha}" -eq 64 ] || exit 1
    music_complete=1
    if [ -f assets/music-required-v1 ]; then
        [ -x greenlink-audio ] && [ -f tools/music.py ] &&
        [ -f scripts/music-runtime.sh ] && [ -x music-runtime/usr/bin/python3 ] || music_complete=0
    fi
    if [ "$music_complete" -eq 1 ] && [ -f .installed-package-version ] &&
       [ "$(cat .installed-package-version)" = "$package_version" ] &&
       [ -s .installed-package-sha256 ] &&
       sha256sum -c .installed-package-sha256 >/dev/null 2>&1; then
        echo 'Complete package already up to date.'; exit 0
    fi
    # The package is bounded independently of the original shell-only executable.
    (ulimit -f 65536; curl -fsSL --proto '=https' --proto-redir '=https' \
        --cacert certs/cacert.pem --connect-timeout 10 --max-time 300 \
        --max-redirs 5 "$base/download/package-$package_version/greenlink.tgz" -o .update-package) || exit 1
    [ "$(wc -c < .update-package)" -le 33554432 ] || exit 1
    echo "$package_sha  .update-package" | sha256sum -c - || exit 1
    mkdir .update-lock/stage
    tar -tzf .update-package | while IFS= read -r entry; do
        case "$entry" in greenlink/*) ;; *) echo "Unsafe package entry: $entry" >&2; exit 1;; esac
        case "$entry" in *../*|*/../*|/*) echo "Unsafe archive path" >&2; exit 1;; esac
    done
    tar -xzf .update-package -C .update-lock/stage
    staged=.update-lock/stage/greenlink
    [ -x "$staged/greenlink" ] && [ -x "$staged/greenlink-catalog" ] || exit 1
    [ "$("$staged/greenlink" --version)" = "$package_version" ] || exit 1
    [ "$("$staged/greenlink-catalog" --version)" = "$package_version" ] || { echo "Catalog worker version does not match package" >&2; exit 1; }
    # Keep old shell for recovery; copy only package-controlled files.
    # Locally downloaded ROMs, settings and private catalog files remain untouched.
    cp greenlink .update-backup
    for part in greenlink greenlink-catalog greenlink-player greenlink-audio greenlink-resolver player-menu-v1; do
        if [ -f "$staged/$part" ]; then cp "$staged/$part" "./$part.new" && chmod 755 "./$part.new" && mv -f "./$part.new" "./$part"; fi
    done
    for part in assets certs music-runtime; do
        if [ -d "$staged/$part" ]; then mkdir -p "$part"; cp -R "$staged/$part/." "$part/"; fi
    done
    for part in scripts/music-runtime.sh scripts/music-qjs.sh tools/music.py; do
        if [ -f "$staged/$part" ]; then
            mkdir -p "$(dirname "$part")"
            cp "$staged/$part" "$part.new" && chmod 755 "$part.new" && mv -f "$part.new" "$part"
        fi
    done
    # Only our two optional cores; leave the user's RetroArch core tree untouched.
    if [ -d "$staged/cores" ]; then
        mkdir -p cores
        for part in snes9x2005_flxtr_libretro.so mame2003_plus_flxtr_libretro.so SNES9X-LICENSE.txt MAME2003-PLUS-LICENSE.md snes9x2005-build.txt mame2003-plus-build.txt; do
            if [ -f "$staged/cores/$part" ]; then
                cp "$staged/cores/$part" "cores/$part.new" && mv -f "cores/$part.new" "cores/$part"
            fi
        done
    fi
    if [ -f "$staged/retroarch-libs/libSDL2-2.0.so.0" ]; then
        mkdir -p retroarch-libs
        for part in libSDL2-2.0.so.0 SDL2-LICENSE.txt; do
            cp "$staged/retroarch-libs/$part" "retroarch-libs/$part.new" && mv -f "retroarch-libs/$part.new" "retroarch-libs/$part"
        done
    fi
    for part in update.sh greenlink.sh controller-idle.sh toc.txt catalog.tsv WABT-LICENSE.txt; do
        if [ -f "$staged/$part" ]; then cp "$staged/$part" "./$part.new" && mv -f "./$part.new" "./$part"; fi
    done
    mv -f .update-backup greenlink.previous
    # A manual install can replace binaries while leaving the version marker.
    # Record hashes from the verified archive, then verify what is on disk.
    (cd "$staged"; for part in greenlink greenlink-catalog greenlink-player greenlink-audio greenlink-resolver player-menu-v1 greenlink.sh update.sh controller-idle.sh retroarch-libs/libSDL2-2.0.so.0 cores/snes9x2005_flxtr_libretro.so cores/mame2003_plus_flxtr_libretro.so; do
        if [ -f "$part" ]; then sha256sum "$part"; fi
    done
    for part in scripts/music-runtime.sh scripts/music-qjs.sh tools/music.py; do
        if [ -f "$part" ]; then sha256sum "$part"; fi
    done
    if [ -d music-runtime ]; then find music-runtime -type f -exec sha256sum {} \;; fi
    ) >.update-lock/package-sha256
    sha256sum -c .update-lock/package-sha256 || exit 1
    mv -f .update-lock/package-sha256 .installed-package-sha256
    printf '%s\n' "$package_version" >.installed-package-version
    sync
    echo "Installed complete FlXtR package $package_version."
    exit 0
fi
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

#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
sdk=${STEAMLINK_SDK_PATH:-/opt/steamlink-sdk}
# Valve's environment setup references optional unset variables.
set +u
source "$sdk/setenv.sh"
set -u
mkdir -p build dist/steamlink/apps/greenlink
version=$(git rev-parse HEAD)
$CC -Os -std=c99 -Wall -Wextra -Werror -DFLXTR_VERSION=\"$version\" $(pkg-config --cflags sdl2) src/shell.c -o build/greenlink-arm $(pkg-config --libs sdl2) -lSDL2_image -lcurl
$STRIP build/greenlink-arm
$CC -Os -std=c99 -Wall -Wextra -Werror -DFLXTR_VERSION=\"$version\" $(pkg-config --cflags sdl2) src/catalog.c -o build/greenlink-catalog-arm -lcurl -ljson-c -lcrypto -lSDL2_image $(pkg-config --libs sdl2)
$STRIP build/greenlink-catalog-arm
app=dist/steamlink/apps/greenlink
if [ -x build/moonlight/moonlight ]; then
    mkdir -p "$app/moonlight" "$app/sources"
    cp build/moonlight/moonlight build/moonlight/flxtr-audio-test build/moonlight/LICENSE build/moonlight/BUILD.txt build/moonlight/SHA256SUMS "$app/moonlight/"
    cp dist/core-sources/moonlight-flxtr-source.tar.gz "$app/sources/"
fi
cp packaging/moonlight-run.sh "$app/"
if [ -f build/greenlink-handoff-game-arm ]; then
    cp build/greenlink-handoff-game-arm "$app/greenlink-handoff-game"
    mkdir -p "$app/cores" "$app/sources"
    cp build/cores/gambatte_handoff_libretro.so build/cores/gambatte-handoff.json build/cores/GAMBATTE-COPYING.txt "$app/cores/"
    cp dist/core-sources/gambatte-handoff-source.tar.gz "$app/sources/"
    cp build/cores/fceumm_handoff_libretro.so build/cores/fceumm-handoff.json build/cores/FCEUMM-COPYING.txt "$app/cores/"
    cp dist/core-sources/fceumm-handoff-source.tar.gz "$app/sources/"
fi
$CC -Os -Wall -Wextra -Werror -fPIC -shared -nostdlib src/samba_paths.c -o "$app/samba-paths.so"
cp packaging/samba-packages.json packaging/LIBRETRO-API-LICENSE.txt "$app/"
if [ ! -f build/cacert.pem ]; then
    wget -q https://curl.se/ca/cacert-2026-09-25.pem -O build/cacert.pem
fi
echo 'a41b5d356aea97a529fe27e0f7316d2f9d946d75927476cf9cf1b90637d00505  build/cacert.pem' | sha256sum -c -
mkdir -p "$app/certs"
cp build/cacert.pem "$app/certs/cacert.pem"
cp packaging/CERTIFICATES.txt "$app/certs/NOTICE.txt"
cp build/greenlink-arm "$app/greenlink"
cp build/greenlink-catalog-arm "$app/greenlink-catalog"
cp packaging/deceptive-dice.sh packaging/deceptive-dice-release.conf packaging/greenlink.sh packaging/power-activity.sh packaging/update.sh packaging/controller-idle.sh packaging/samba-run.sh packaging/samba-server.sh packaging/reset-scummvm-config.sh packaging/toc.txt "$app/"
cp catalog.tsv "$app/catalog.tsv"
# RetroArch 1.7.x predates the firmware's SDL2-to-SDL3 compatibility layer.
# Pin Valve's native SDL2 build and load it only in the RetroArch child.
sdl=build/retroarch-libs/libSDL2-2.0.so.0
mkdir -p build/retroarch-libs "$app/retroarch-libs"
if [ ! -f "$sdl" ]; then
    wget -q --tries=3 --timeout=120 \
        https://raw.githubusercontent.com/ValveSoftware/steamlink-sdk/62b4d098d1472c3534dd098ca2a0e0e10712f1c6/rootfs/usr/lib/libSDL2-2.0.so.0.3100.0 -O "$sdl"
fi
echo "c9d19c7ae28243b3739e2f14a4f19cdd48646bed063b26607d25b19dbc148932  $sdl" | sha256sum -c -
cp "$sdl" "$app/retroarch-libs/"
cp packaging/SDL2-LICENSE.txt "$app/retroarch-libs/"
if [ -f build/test-steamlink-audio ]; then
    mkdir -p "$app/scripts"
    cp build/test-steamlink-audio "$app/"
    cp build/steamlink_audio_test_libretro.so "$app/"
    cp scripts/test-steamlink-audio "$app/scripts/"
    chmod +x "$app/scripts/test-steamlink-audio"
fi
if [ -f build/retroarch-audio/retroarch.exec ]; then
    mkdir -p "$app/retroarch-audio" "$app/sources"
    cp build/retroarch-audio/retroarch.exec build/retroarch-audio/COPYING build/retroarch-audio/BUILD.txt "$app/retroarch-audio/"
    cp dist/core-sources/retroarch-audio.tar.gz "$app/sources/"
    cp scripts/build-retroarch-audio.sh "$app/sources/"
fi

if [ -d assets ]; then cp -R assets "$app/"; fi
if [ -x "$app/moonlight/moonlight" ]; then
    cp packaging/moonlight-bootstrap.sh "$app/assets/"
    tar -C "$app" -czf "$app/assets/moonlight-bootstrap.tgz" moonlight-run.sh \
        moonlight/moonlight moonlight/flxtr-audio-test moonlight/LICENSE moonlight/BUILD.txt moonlight/SHA256SUMS
    (cd "$app" && sha256sum assets/moonlight-bootstrap.tgz >assets/moonlight-bootstrap.sha256)
fi
if [ -f build/greenlink-player-arm ]; then cp build/greenlink-player-arm "$app/greenlink-player"; fi
if [ -f build/greenlink-download-arm ]; then cp build/greenlink-download-arm "$app/greenlink-download"; fi
if [ -f build/greenlink-resolver-arm ]; then
    cp build/greenlink-resolver-arm "$app/greenlink-resolver"
    cp build/resolver/WABT-LICENSE.txt "$app/WABT-LICENSE.txt"
fi
# Optional side-by-side cores from scripts/build-cores.sh. No system core is replaced.
for core in snes9x2005_flxtr_libretro.so snes9x2005_plus_flxtr_libretro.so mame2003_plus_flxtr_libretro.so; do
    if [ -f "build/cores/$core" ]; then
        mkdir -p "$app/cores"
        cp "build/cores/$core" "$app/cores/"
    fi
done
if [ -d "$app/cores" ]; then
    for notice in SNES9X-LICENSE.txt MAME2003-PLUS-LICENSE.md snes9x2005-build.txt snes9x2005_plus-build.txt mame2003-plus-build.txt; do
        if [ -f "build/cores/$notice" ]; then cp "build/cores/$notice" "$app/cores/"; fi
    done
fi
if [ -f build/greenlink-audio-arm ]; then cp build/greenlink-audio-arm "$app/greenlink-audio"; fi
if [ -f build/music-runtime.tgz ]; then
    mkdir -p "$app/music-runtime" "$app/tools" "$app/scripts"
    tar -xzf build/music-runtime.tgz -C "$app/music-runtime"
    cp tools/music.py tools/samba_install.py "$app/tools/"
    cp scripts/music-runtime.sh scripts/music-qjs.sh "$app/scripts/"
    chmod +x "$app/scripts/music-runtime.sh" "$app/scripts/music-qjs.sh"
    printf '1\n' > "$app/assets/music-required-v1"
fi
mkdir -p "$app/tools" "$app/scripts"
cp tools/music.py tools/psp_core.py tools/rom_assets.py tools/state_guard.py tools/rom_music.py tools/usb_setup.py tools/usb_mount_template.sh tools/handoff.py tools/rom_transfer.py tools/tailscale_link.py "$app/tools/"
# Older updaters copy the complete Python runtime before they know new tools.
mkdir -p "$app/music-runtime/usr/lib/python3.12/site-packages"
cp tools/rom_transfer.py "$app/music-runtime/usr/lib/python3.12/site-packages/rom_transfer.py"
for tool in rom_assets state_guard rom_music psp_core; do
    cp "tools/$tool.py" "$app/music-runtime/usr/lib/python3.12/site-packages/$tool.py"
done
cp packaging/tailscale-arm.json "$app/tailscale-arm.json"
mkdir -p "$app/assets"
cp packaging/psp-core.json "$app/assets/psp-core.json"
cp scripts/music-runtime.sh scripts/music-qjs.sh "$app/scripts/"
if [ -f build/greenlink-preview-arm ]; then cp build/greenlink-preview-arm "$app/greenlink-preview"; fi
if [ -f build/player-menu-v1 ]; then cp build/player-menu-v1 "$app/player-menu-v1"; fi
if [ "${FLXTR_DISABLE_AUTO_UPDATE:-0}" = 1 ]; then
    printf 'Experimental manual test package; enable updates in Settings if desired.\n' > "$app/.no-auto-update"
fi
cp docs/DESKTOP-CONTROLS.md "$app/CONTROLS.md"
cp docs/STEAMLINK-PLAYBACK.md "$app/PLAYBACK.md"
chmod +x "$app/greenlink" "$app/greenlink.sh"
# Keep provenance inside the archive so deployment can verify the actual payload.
printf '%s\n' "$version" > "$app/build-version.txt"
git diff --quiet && git diff --cached --quiet && source_dirty=0 || source_dirty=1
printf '%s\n' "$source_dirty" > "$app/build-source-dirty.txt"
(cd "$app"; for part in moonlight-run.sh moonlight/moonlight moonlight/flxtr-audio-test moonlight/LICENSE moonlight/BUILD.txt moonlight/SHA256SUMS sources/moonlight-flxtr-source.tar.gz greenlink greenlink-catalog greenlink-player greenlink.sh power-activity.sh tools/usb_setup.py tools/usb_mount_template.sh greenlink-handoff-game tools/handoff.py tools/rom_transfer.py tools/tailscale_link.py tailscale-arm.json cores/gambatte_handoff_libretro.so cores/gambatte-handoff.json cores/GAMBATTE-COPYING.txt sources/gambatte-handoff-source.tar.gz cores/fceumm_handoff_libretro.so cores/fceumm-handoff.json cores/FCEUMM-COPYING.txt sources/fceumm-handoff-source.tar.gz; do
    if [ -f "$part" ]; then sha256sum "$part"; fi
done) > "$app/build-sha256.txt"
tar -C dist/steamlink/apps -czf dist/greenlink.tgz greenlink
file build/greenlink-arm
wc -c build/greenlink-arm dist/greenlink.tgz

{ printf "version=%s\n" "$version"; printf "sha256=%s\n" "$(sha256sum build/greenlink-arm | cut -d " " -f1)"; } > dist/shell-manifest.txt

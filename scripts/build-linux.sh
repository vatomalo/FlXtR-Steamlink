#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
arch=$(uname -m)
case "$arch" in x86_64|aarch64) ;; *) echo "Unsupported architecture: $arch" >&2; exit 1;; esac
version=$(git rev-parse HEAD)
build=$(date +%s)
target=linux-$arch
root=dist/FlXtR-$target
app=$root/releases/$version
mkdir -p "$app" build
flags=(-Os -std=c99 -Wall -Wextra -Werror -DFLXTR_DESKTOP "-DFLXTR_VERSION=\"$version\"")
cc "${flags[@]}" $(pkg-config --cflags sdl2) src/shell.c -o "$app/greenlink" $(pkg-config --libs sdl2 libcurl)
cc "${flags[@]}" src/catalog.c -o "$app/greenlink-catalog" $(pkg-config --cflags --libs sdl2 SDL2_image libcurl json-c openssl)
cc "${flags[@]}" src/player_linux.c -o "$app/greenlink-player" $(pkg-config --cflags --libs mpv)
strip "$app/greenlink" "$app/greenlink-catalog" "$app/greenlink-player"
FLXTR_BUILD_PLATFORM=linux bash scripts/build-resolver.sh
cp build/greenlink-resolver-linux "$app/greenlink-resolver"
cp build/resolver/WABT-LICENSE.txt "$app/"
cp -R assets "$app/"
cp catalog.tsv packaging/linux-controls.lua "$app/"
cp LICENSE "$app/"
cp packaging/linux-update.py "$app/update.py"
cp packaging/linux-update.sh "$app/update.sh"
cp packaging/flxtr-linux.sh "$root/FlXtR.sh"
chmod +x "$root/FlXtR.sh" "$app/update.sh"
printf '1\n' > "$app/player-menu-v1"
printf '{"platform":"%s","version":"%s","build":%s}\n' "$target" "$version" "$build" > "$app/build.json"
ln -sfn "releases/$version" "$root/current"
tar -C "$app" -czf "dist/$target.tgz" .
sha=$(sha256sum "dist/$target.tgz" | cut -d' ' -f1)
printf '{"platform":"%s","version":"%s","build":%s,"sha256":"%s"}\n' "$target" "$version" "$build" "$sha" > "dist/$target.json"
tar -C dist -czf "dist/FlXtR-$target-install.tgz" \
    "FlXtR-$target/FlXtR.sh" "FlXtR-$target/current" "FlXtR-$target/releases/$version"
echo "Built $root/FlXtR.sh"
if [ -n "${FLXTR_LINUX_OUTPUT_DIR:-}" ]; then
    output=$FLXTR_LINUX_OUTPUT_DIR
    if [ ! -d "$output" ] && [ ! -w "$(dirname "$output")" ]; then
        sudo install -d -o "$(id -u)" -g "$(id -g)" "$output"
    fi
    mkdir -p "$output/releases"
    [ -w "$output" ] || { echo "Output directory is not writable: $output" >&2; exit 1; }
    stage=$(mktemp -d "$output/releases/$version.XXXXXX")
    cp -R "$app/." "$stage/"
    # Switch all matching helpers together; never overwrite a running ELF file.
    ln -s "releases/$(basename "$stage")" "$output/.current-next-$$"
    mv -Tf "$output/.current-next-$$" "$output/current"
    cp packaging/flxtr-linux.sh "$output/FlXtR.sh"
    chmod +x "$output/FlXtR.sh"
    for item in greenlink greenlink-catalog greenlink-player greenlink-resolver; do
        ln -sfn "current/$item" "$output/$item"
    done
    printf 'Linux executable: %s/greenlink\nRun the complete app: %s/FlXtR.sh\n' "$output" "$output"
fi

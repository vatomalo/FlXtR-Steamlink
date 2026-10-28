#!/bin/sh
# Fixed release and checksum are generated when packaging the game.
set -eu
base=$(pwd)
installed="$base/native-games/deceptive-dice"
if [ ! -x "$installed/deceptive-dice" ]; then
    . "$base/deceptive-dice-release.conf"
    mkdir -p "$base/native-games"
    stage=$(mktemp -d "$base/native-games/.dice-XXXXXX")
    trap 'rm -rf "$stage"' EXIT HUP INT TERM
    curl --fail --location --proto '=https' --proto-redir '=https' --connect-timeout 20 --max-time 900 \
        --cacert "$base/certs/cacert.pem" "$DICE_URL" -o "$stage/game.tgz"
    printf '%s  %s\n' "$DICE_SHA256" "$stage/game.tgz" | sha256sum -c -
    # Reject unexpected roots, traversal, absolute names and link entries.
    tar -tzf "$stage/game.tgz" > "$stage/files"
    awk 'BEGIN {bad=0} /^\// || /(^|\/)\.\.(\/|$)/ || !/^deceptive-dice(\/|$)/ {bad=1} END {exit bad}' "$stage/files"
    tar -tvzf "$stage/game.tgz" | awk 'substr($0,1,1)!="-" && substr($0,1,1)!="d" {bad=1} END {exit bad}'
    tar -xzf "$stage/game.tgz" -C "$stage"
    test -x "$stage/deceptive-dice/deceptive-dice"
    test ! -e "$installed"
    mv "$stage/deceptive-dice" "$installed"
    rm -rf "$stage"
    trap - EXIT HUP INT TERM
fi
cd "$installed"
exec sh ./launch.sh

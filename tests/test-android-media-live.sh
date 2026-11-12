#!/usr/bin/env bash
# Optional network regression test for KissAnime and Internet Archive.
set -euo pipefail
repo=$(cd "$(dirname "$0")/.." && pwd)
cd "$repo"
json=$(find "${GRADLE_USER_HOME:-$HOME/.gradle}/caches/modules-2/files-2.1/org.json/json/20240303" -name '*.jar' -print -quit)
jsoup=$(find "${GRADLE_USER_HOME:-$HOME/.gradle}/caches/modules-2/files-2.1/org.jsoup/jsoup/1.18.3" -name '*.jar' -print -quit)
[[ -n "$json" && -n "$jsoup" ]]
build="$repo/build/android-media-smoke"
mkdir -p "$build/classes"
javac -cp "$json:$jsoup" -d "$build/classes" \
    android/app/src/main/java/org/flxtr/android/{CatalogItem,CatalogPage,MediaCaptions,MediaHttp,StreamProtocol,StreamDecoder,StreamResolver,KissAnime,ArchiveMedia}.java \
    tests/android/MediaResolverSmoke.java
java -cp "$build/classes:$json:$jsoup" org.flxtr.android.MediaResolverSmoke

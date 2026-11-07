#!/usr/bin/env bash
# Use SkyLoft's own GitHub login; do not depend on laptop credential forwarding.
set +x
set -euo pipefail
storage=${FLXTR_STORAGE:-/opt/dockers/storage}
repo=${FLXTR_BUILD_SOURCE:-$storage/projects/FlXtR/source}
cli=$storage/tools/bin/gh
[[ -x "$cli" ]] || cli=$(command -v gh)
case "${1:-}" in --check|--android|--steamlink|--linux|--windows) action=$1;; *) echo 'Choose --check, --android, --steamlink, --linux or --windows.' >&2; exit 2;; esac
GH_TOKEN=${GH_TOKEN:-${GITHUB_TOKEN:-}}
if [[ -z "$GH_TOKEN" ]]; then GH_TOKEN=$("$cli" auth token --hostname github.com 2>/dev/null) || true; fi
[[ -n "$GH_TOKEN" ]] || { echo 'SkyLoft needs GitHub login. Run: gh auth login --hostname github.com --git-protocol https --web (or build with --no-publish).' >&2; exit 1; }
export GH_TOKEN
trap 'unset GH_TOKEN' EXIT
export PATH="$(dirname "$cli"):$PATH"
project=vatomalo/FlXtR-Steamlink
if [[ "$action" == --check ]]; then
    access=$("$cli" api "repos/$project" --jq '.permissions.push')
    [[ "$access" == true ]] || { echo 'This GitHub account cannot publish FlXtR releases.' >&2; exit 1; }
    echo 'GitHub publishing access is ready.'
    exit 0
fi
cd "$repo"
[[ -z "$(git status --porcelain)" ]] || { echo 'Source changed during the build; refusing to publish.' >&2; exit 1; }
sha=$(git rev-parse HEAD)
if [[ "$action" == --linux || "$action" == --windows ]]; then
    python3 scripts/publish-desktop.py "${action#--}"
    exit 0
fi
if [[ "$action" == --android ]]; then
    echo 'Publishing Android update…'
    bash "$storage/projects/FlXtR/check-signing.sh"
    notes_mount=()
    if [[ -n ${FLXTR_RELEASE_NOTES_FILE:-} ]]; then
        notes_mount=(-v "$FLXTR_RELEASE_NOTES_FILE:/release/notes.md:ro" -e FLXTR_RELEASE_NOTES_FILE=/release/notes.md)
    fi
    docker run --rm -e GH_TOKEN -e "GITHUB_SHA=$sha" -e FLXTR_RELEASE_TITLE -e FLXTR_RELEASE_ARCH \
        "${notes_mount[@]}" \
        -v "$repo:/src" -v "$cli:/usr/local/bin/gh:ro" \
        flxtr-android-builder:skyloft python3 scripts/publish-android.py \
        --apk dist/FlXtR-Android-preview.apk --sha "$sha"
else
    # Steam Link release tags identify source commits. Keep existing published
    # payloads immutable when a user builds the same commit again.
    tag="package-$sha"
    if release=$("$cli" release view "$tag" -R "$project" --json isDraft,url 2>/dev/null); then
        python3 - "$release" <<'EXISTING'
import json, sys
release = json.loads(sys.argv[1])
if release['isDraft']:
    raise SystemExit('A draft exists for this commit. Finish or remove it before retrying publication.')
print('This Steam Link commit is already published; existing release kept: ' + release['url'])
EXISTING
        edit=()
        if [[ -n ${FLXTR_RELEASE_TITLE:-} ]]; then edit+=(--title "$FLXTR_RELEASE_TITLE"); fi
        if [[ -n ${FLXTR_RELEASE_NOTES_FILE:-} ]]; then edit+=(--notes-file "$FLXTR_RELEASE_NOTES_FILE"); fi
        if ((${#edit[@]})); then "$cli" release edit "$tag" -R "$project" "${edit[@]}"; fi
    else
        echo 'Publishing Steam Link update…'
        python3 scripts/publish-package.py
    fi
fi

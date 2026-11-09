#!/bin/sh
# Older FlXtR updaters copy assets recursively but do not know new executables.
# This small compatibility bundle lets their next launch finish the migration.
set -eu
bundle=assets/moonlight-bootstrap.tgz
[ -r "$bundle" ] || exit 0
if cmp -s assets/moonlight-bootstrap.sha256 moonlight/bootstrap.sha256 2>/dev/null && \
    (cd moonlight && sha256sum -c SHA256SUMS >/dev/null 2>&1) && [ -r moonlight-run.sh ]; then
    exit 0
fi
sha256sum -c assets/moonlight-bootstrap.sha256
stage=.moonlight-bootstrap-$$
mkdir "$stage"
trap 'rm -rf "$stage"' EXIT
tar -xzf "$bundle" -C "$stage"
(cd "$stage/moonlight" && sha256sum -c SHA256SUMS)
mkdir -p moonlight
for part in moonlight flxtr-audio-test LICENSE BUILD.txt SHA256SUMS; do
    cp -p "$stage/moonlight/$part" "moonlight/$part.next"
    mv -f "moonlight/$part.next" "moonlight/$part"
done
cp -p "$stage/moonlight-run.sh" moonlight-run.sh.next
mv -f moonlight-run.sh.next moonlight-run.sh
cp assets/moonlight-bootstrap.sha256 moonlight/bootstrap.sha256

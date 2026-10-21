#!/usr/bin/env bash
# Run on the Docker host. apk verifies ARM packages; no emulator or daemon on Link.
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build/music-arm-keys build/music-runtime
image=alpine:3.23@sha256:85fe1e81d6758c208f3e1eed4338a1997e19d4be002d4dd32d3100c9a8c010a0
source=$(docker create --platform linux/arm/v7 alpine:3.21@sha256:ce64758a109eb420d874a118f87920e625e12d3634e03b4a5573fd9f6e5d3507)
trap 'docker rm "$source" >/dev/null' EXIT
docker cp "$source:/etc/apk/keys/." build/music-arm-keys/
curl -fLsS https://github.com/yt-dlp/yt-dlp/releases/download/2026.08.19/yt-dlp -o build/yt-dlp
echo '1fa6733c37ea6fb51c99ad8fe785e7b7e5f3246c9b980230329d4fb72ed8d4d6  build/yt-dlp' | sha256sum -c -
curl -fLsS https://files.pythonhosted.org/packages/e3/bd/520769863744b669440a924271a6159ddd82ad5ae26b4ac4d4b69e9f8d44/yt_dlp_ejs-0.8.0-py3-none-any.whl -o build/yt-dlp-ejs.whl
echo '79300e5fca7f937a1eeede11f0456862c1b41107ce1d726871e0207424f4bdb4  build/yt-dlp-ejs.whl' | sha256sum -c -
docker run --rm -v "$PWD/build:/work" -v "$PWD/tools:/tools:ro" "$image" sh -ec '
    r=/tmp/music-runtime
    mkdir -p "$r/etc/apk/keys"
    cp /work/music-arm-keys/* "$r/etc/apk/keys/"
    apk --arch armv7 --root "$r" --initdb --no-scripts --repositories-file /etc/apk/repositories add python3 quickjs
    apk add --no-cache python3
    python3 /tools/package_music.py /work/yt-dlp "$r/usr/lib/python3.12/site-packages"
    python3 -m zipfile -e /work/yt-dlp-ejs.whl "$r/usr/lib/python3.12/site-packages"
    PYTHONPATH="$r/usr/lib/python3.12/site-packages" python3 -c "import yt_dlp, yt_dlp_ejs; print(yt_dlp.version.__version__)"
    apk --root "$r" info -v > "$r/PACKAGES.txt"
    printf "yt-dlp 2026.08.19\nAlpine 3.23 armv7 / isolated musl loader\n" > "$r/BUILD.txt"
    cp "$r/lib/apk/db/installed" "$r/APK-PROVENANCE.txt"
    python3 /tools/music_notices.py "$r"
    tar --exclude=usr/lib/python3.12/ensurepip --exclude="usr/lib/libgdbm*" --exclude="usr/lib/libreadline*" \
        --exclude="usr/lib/python3.12/lib-dynload/_gdbm*" --exclude="usr/lib/python3.12/lib-dynload/readline*" \
        -C "$r" -czf /work/music-runtime.tgz.next lib usr PACKAGES.txt BUILD.txt APK-PROVENANCE.txt LICENSES
    mv /work/music-runtime.tgz.next /work/music-runtime.tgz
'
printf 'Runtime ready: build/music-runtime.tgz\n'

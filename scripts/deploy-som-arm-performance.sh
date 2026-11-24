#!/usr/bin/env bash
# Install the Steam Link performance work (docs/SOM-ARM-DEPLOYMENT.md).
#
#   bash scripts/deploy-som-arm-performance.sh [--host root@IP] server|proxy|all
#
# server  tools/som_wram_server.py -> /home/apps/greenlink/tools/ (safe while
#         the game runs; bridges pick it up on their next SSH session)
# proxy   core, ROM-derived weapon masks and their opt-in marker from
#         build/som-ai/ -> cores/experimental/; refused while RetroArch runs
#         the proxy
#
# Every install: device identity check, a full read-only snapshot with
# scripts/som-backup-steamlink.sh, an on-device copy of the file it replaces
# (<name>.pre-armperf-<stamp>), upload under a temporary name, SHA-256 check,
# permissions copied, atomic rename on the same filesystem, and a record in
# cores/experimental/backups/som-arm-perf-deploy.txt that
# scripts/rollback-som-arm-performance.sh reads. Saves, ROMs, the AI memory
# database and configuration are never touched.
set -euo pipefail
cd "$(dirname "$0")/.."
host=root@100.124.74.13
if [ "${1:-}" = --host ]; then host=$2; shift 2; fi
what=${1:-}
case "$what" in server|proxy|all) ;; *) sed -n 2,18p "$0"; exit 2;; esac
E=/home/apps/retroarch/cores/experimental
G=/home/apps/greenlink
ssh_() { ssh -o BatchMode=yes -o ConnectTimeout=8 "$host" "$@"; }
stamp=$(date +%Y%m%d-%H%M%S)
git_sha=$(git rev-parse --short HEAD)
[ -z "$(git status --porcelain -- src tools/som_wram_server.py)" ] ||
  echo "warning: uncommitted changes under src/ or the server; the record names $git_sha" >&2

# 1. device identity: the Steam Link (ARMv7, its RetroArch tree)
ident=$(ssh_ "uname -m; [ -d $E ] && [ -d $G/tools ] && echo layout-ok; cat /proc/cpuinfo | grep -c '^processor'")
case "$ident" in armv7l*layout-ok*) ;; *) echo "refusing: $host is not the expected Steam Link: $ident" >&2; exit 1;; esac

install_file() {   # local remote-path mode
  local local_file=$1 remote=$2 mode=$3 sum remote_sum
  sum=$(sha256sum "$local_file" | cut -c1-64)
  scp -q -o BatchMode=yes "$local_file" "$host:$remote.armperf-new"
  remote_sum=$(ssh_ "sha256sum '$remote.armperf-new' | cut -c1-64")
  if [ "$sum" != "$remote_sum" ]; then
    ssh_ "rm -f '$remote.armperf-new'"; echo "refusing: checksum mismatch for $remote" >&2; exit 1
  fi
  ssh_ "set -e; f='$remote'; n=\"\$f.armperf-new\"
        if [ -e \"\$f\" ]; then cp -p \"\$f\" \"\$f.pre-armperf-$stamp\"; chmod --reference=\"\$f\" \"\$n\" 2>/dev/null || chmod $mode \"\$n\"
        else chmod $mode \"\$n\"; fi
        mv -f \"\$n\" \"\$f\"
        mkdir -p $E/backups
        old=none; [ ! -e \"\$f.pre-armperf-$stamp\" ] || old=\"\$f.pre-armperf-$stamp\"
        echo \"$stamp $git_sha \$f $sum \$old\" >> $E/backups/som-arm-perf-deploy.txt"
  echo "installed $remote ($sum) from $git_sha"
}

if [ "$what" = proxy ] || [ "$what" = all ]; then
  so=build/som-ai/som_ai_proxy_libretro.so
  [ -f "$so" ] || { echo "build it first: bash scripts/build-som-ai-proxy.sh" >&2; exit 1; }
  masks=build/som-ai/som_weapon_masks.bin
  marker=build/som-ai/som_ai_weapon_masks_on
  [ -f "$masks" ] && [ -f "$marker" ] || {
    echo "build the ROM-derived weapon mask pack first with SOM_ROM_PATH set" >&2; exit 1;
  }
  file "$so" | grep -q 'ARM, EABI5' || { echo "refusing: $so is not an ARM EABI5 build" >&2; exit 1; }
  # 2./3. active core path, and never while RetroArch has the proxy loaded
  running=$(ssh_ "ps | grep '[r]etroarch.exec' | grep -c som_ai_proxy_libretro.so || true")
  if [ "$running" != 0 ]; then
    echo "refusing: RetroArch is running the proxy; close the game first" >&2; exit 1
  fi
fi
# 4.-6. full read-only snapshot (core, configs, saves) with checksums
bash scripts/som-backup-steamlink.sh "$host" >/dev/null
echo "snapshot written under ~/backups/som-steamlink/"
if [ "$what" = server ] || [ "$what" = all ]; then
  install_file tools/som_wram_server.py "$G/tools/som_wram_server.py" 644
fi
if [ "$what" = proxy ] || [ "$what" = all ]; then
  install_file build/som-ai/som_ai_proxy_libretro.so "$E/som_ai_proxy_libretro.so" 755
  install_file build/som-ai/som_weapon_masks.bin "$E/som_weapon_masks.bin" 644
  install_file build/som-ai/som_ai_weapon_masks_on "$E/som_ai_weapon_masks_on" 644
fi
echo "rollback: bash scripts/rollback-som-arm-performance.sh${host:+ --host $host}"

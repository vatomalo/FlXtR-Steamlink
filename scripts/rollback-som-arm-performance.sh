#!/usr/bin/env bash
# Undo scripts/deploy-som-arm-performance.sh (docs/SOM-ARM-DEPLOYMENT.md).
#
#   bash scripts/rollback-som-arm-performance.sh [--host root@IP] [--dry-run]
#
# Reads cores/experimental/backups/som-arm-perf-deploy.txt and, for every
# file installed since the last rollback, restores the state before the
# FIRST of those installs: the .pre-armperf-<stamp> copy it replaced (atomic
# rename of a copy, so the backup stays) or, for a file that did not exist
# before (the observation server), moves it aside to
# <name>.rolled-back-<stamp>. Bridges then fall back to the shell loop on their
# next SSH session. Also removes the timing marker /tmp/flxtr-som-perf-enable.
#
# A replaced proxy takes effect at the next game launch (RetroArch keeps the
# old one mapped). Saves, ROMs, configuration and the AI memory database
# (~/.local/share/flxtr/som-ai/ on the PC) are never touched.
set -euo pipefail
host=root@100.124.74.13
dry=0
while [ $# -gt 0 ]; do
  case "$1" in
    --host) host=$2; shift 2;;
    --dry-run) dry=1; shift;;
    *) sed -n 2,16p "$0"; exit 2;;
  esac
done
E=/home/apps/retroarch/cores/experimental
ssh_() { ssh -n -o BatchMode=yes -o ConnectTimeout=8 "$host" "$@"; }   # -n: never eat the loop input
record=$(ssh_ "cat $E/backups/som-arm-perf-deploy.txt 2>/dev/null" || true)
[ -n "$record" ] || { echo "nothing to roll back: no deploy record on $host"; exit 0; }
stamp=$(date +%Y%m%d-%H%M%S)
done_files=" "
# entries after the last rollback line, oldest first; the first per file wins
pending=$(printf '%s\n' "$record" | awk '$2=="rollback"{n=0; next} {l[n++]=$0} END{for(i=0;i<n;i++)print l[i]}')
[ -n "$pending" ] || { echo "nothing to roll back since the last rollback"; exit 0; }
while read -r when sha path sum old; do
  case "$done_files" in *" $path "*) continue;; esac
  done_files="$done_files$path "
  case "$path" in
    $E/som_ai_proxy_libretro.so|$E/som_weapon_masks.bin|$E/som_ai_weapon_masks_on|\
    /home/apps/greenlink/tools/som_wram_server.py) ;;
    *) echo "skipping unexpected path in record: $path" >&2; continue;;
  esac
  if [ "$old" = none ]; then
    echo "move aside $path (first installed $when from $sha; did not exist before)"
    [ $dry = 1 ] || ssh_ "[ ! -e '$path' ] || mv -f '$path' '$path.rolled-back-$stamp'"
  else
    echo "restore $path from $old (first replaced $when by $sha)"
    [ $dry = 1 ] || ssh_ "set -e; [ -f '$old' ]; cp -p '$old' '$path.rollback-tmp'; mv -f '$path.rollback-tmp' '$path'; sha256sum '$path'"
  fi
done <<< "$pending"
[ $dry = 1 ] || ssh_ "rm -f /tmp/flxtr-som-perf-enable; echo \"$stamp rollback\" >> $E/backups/som-arm-perf-deploy.txt"
[ $dry = 1 ] && echo "(dry run: nothing changed)" || echo "rolled back; restart the game to load a restored proxy"

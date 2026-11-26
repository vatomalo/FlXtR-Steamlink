#!/bin/bash
# Real-ROM exchange benchmark of the enemy tactics layer (src/som_enemy_tactics.h).
# One hero against the monster in SLOT of a field save state, the companions
# moved away, the monster kept alive and the hero's HP refilled; 8 starts x 4
# hero swing rhythms, 1800 frames each, per difficulty. Bare SNES core (no
# proxy): only the native AI and the tactics layer act.
#
#   tools/som_headless/tactics_bench.sh CORE.so ROM CORE_STATE [SLOT]
#
# CORE_STATE is a core-only state (a proxy state minus the proxy's trailer:
# head -c $(core serialize size)). Prints attacks, damage dealt to the hero,
# damage taken and their ratio. User-supplied ROM; nothing is written.
set -eu
core=$1 rom=$2 state=$3 slot=${4:-2}
here=$(cd "$(dirname "$0")" && pwd)
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT
cc -O1 -I"$here/../../src" -I"$here/../../src/vendor" "$here/tactics_probe.c" -o "$out/probe" -ldl
for m in base tac:easy tac:normal tac:hard tac:expert; do
  A=0 D=0 T=0
  for sw in 0 35 60 90; do for pos in "60 0" "-50 0" "0 50" "0 -50" "40 40" "-40 30" "80 10" "20 -70"; do
    r=$(GOD=1 HEROHP=1 QUIET=1 SWING=$sw "$out/probe" "$core" "$rom" "$state" $m 1800 "$slot" $pos 2>&1 >/dev/null)
    A=$((A+$(echo "$r"|sed -n 's/.*attacks *\([0-9]*\).*/\1/p')))
    D=$((D+$(echo "$r"|sed -n 's/.*dealt *\([0-9]*\).*/\1/p')))
    T=$((T+$(echo "$r"|sed -n 's/.*taken *\([0-9]*\).*/\1/p')))
  done; done
  printf "%-11s attacks %4d  dealt %6d  taken %6d  ratio %s\n" "$m" $A $D $T \
    "$(awk -v d=$D -v t=$T 'BEGIN{printf "%.3f", d/(t?t:1)}')"
done

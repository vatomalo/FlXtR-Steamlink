#!/usr/bin/env bash
# Run on a machine with SSH access to the Steam Link, while reproducing A/V drift.
# Reads device state and logs only; does not install, restart or clear anything.
set -euo pipefail
if [[ $# -gt 2 ]]; then
    echo 'Usage: capture-timing.sh [user@steamlink-address] [output-file]' >&2
    exit 2
fi
target=${1:-root@192.168.10.2}
if [[ $target == -* || $target == *[[:space:]]* ]]; then
    echo 'Use a single SSH destination, without options or whitespace.' >&2
    exit 2
fi
output=${2:-steamlink-timing-$(date -u +%Y%m%dT%H%M%SZ).log}
umask 077
# noclobber keeps previous evidence intact.
set -o noclobber
ssh -o BatchMode=yes -o ConnectTimeout=10 -o ServerAliveInterval=10 -o ServerAliveCountMax=2 "$target" 'sh -s' > "$output" <<'REMOTE'
set -eu
cd /home/apps/greenlink || exit 1
printf 'READ-ONLY STEAM LINK TIMING CAPTURE\n'
date -u
uname -a
printf '\nPLAYER BUILD\n'
ls -l greenlink-player 2>/dev/null || true
if command -v sha256sum >/dev/null 2>&1; then sha256sum greenlink-player 2>/dev/null || true; fi
snapshot() {
    printf '\nSNAPSHOT %s\n' "$1"
    date -u
    uptime
    printf '\nPLAYER PROCESS IDS\n'
    pidof greenlink-player 2>/dev/null || true
    for file in timing.log timing.previous.log player.log adaptive-quality.log adaptive-quality.previous.log subtitle-debug.log tv-preload.log; do
        if test -f "$file"; then
            printf '\nBEGIN %s\n' "$file"
            tail -n 2000 "$file"
            printf '\nEND %s\n' "$file"
        fi
    done
}
snapshot before
sleep 60
snapshot after
REMOTE
printf 'Saved capture to %s\n' "$output"
echo 'Logs may contain signed media URLs. Keep the original private and redact URLs before sharing.'

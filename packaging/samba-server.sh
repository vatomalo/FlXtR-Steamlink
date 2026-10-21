#!/bin/sh
# Opt-in, app-owned SMB2/SMB3 server; system passwords/config are untouched.
set -eu
umask 077
BASE=$(CDPATH= cd -- "$(dirname "$0")" && pwd -P)
cd "$BASE"
STATE="$BASE/samba"
CONF="$STATE/smb.conf"
RUN="$BASE/samba-run.sh"
pidfile="$STATE/run/smbd.pid"
fail() { echo "$*" >&2; exit 2; }
running() {
    [ -r "$pidfile" ] || return 1
    pid=$(cat "$pidfile")
    case "$pid" in ''|*[!0-9]*) return 1;; esac
    [ "$pid" -gt 1 ] && kill -0 "$pid" 2>/dev/null || return 1
    [ -r "/proc/$pid/cmdline" ] || return 1
    tr '\000' '\n' <"/proc/$pid/cmdline" | grep -F -x -- "$CONF" >/dev/null
}
helpers() {
    mkdir -p "$STATE/helpers"
    for executable in "$BASE"/samba-runtime/usr/bin/* "$BASE"/samba-runtime/usr/sbin/* "$BASE"/samba-runtime/usr/lib/samba/*; do
        [ -f "$executable" ] && [ -x "$executable" ] || continue
        name=${executable##*/}
        case "$name" in *[!a-zA-Z0-9_-]*) continue;; esac
        printf '%s\n' '#!/bin/sh' 'BASE=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd -P)'             "exec /bin/sh \"\$BASE/samba-run.sh\" $name \"\$@\"" >"$STATE/helpers/$name"
        chmod 700 "$STATE/helpers/$name"
    done
}
case "${1:-status}" in
status)
    if running; then echo "RUNNING"; else echo "STOPPED"; fi
    ;;
check)
    sh "$RUN" smbd -V
    [ ! -f "$CONF" ] || sh "$RUN" testparm -s "$CONF"
    ;;
configure)
    [ "$(id -u)" = 0 ] || fail "Run configure as root on the Steam Link"
    running && fail "Stop the FlXtR Samba server before changing configuration"
    [ -f "$BASE/samba-runtime/flxtr-samba-paths.so" ] || fail "Install the Steam Link Samba add-on first"
    mkdir -p "$STATE/private" "$STATE/locks" "$STATE/cache" "$STATE/run" "$STATE/logs" "$STATE/ncalrpc" "$BASE/roms" "$BASE/videos"
    chmod 755 "$STATE" "$STATE/locks" "$STATE/cache" "$STATE/run" "$STATE/ncalrpc"
    chmod 700 "$STATE/private" "$STATE/logs"
    helpers
    port=${FLXTR_SAMBA_PORT:-445}
    case "$port" in ''|*[!0-9]*) fail "Invalid SMB port";; esac
    [ "$port" -gt 0 ] && [ "$port" -le 65535 ] || fail "Invalid SMB port"
    interfaces=${FLXTR_SAMBA_INTERFACES:-lo eth0 wlan0}
    case "$BASE$interfaces" in *'
'*) fail "Invalid configuration path/interface";; esac
    cat >"$CONF.next" <<EOF
[global]
workgroup = WORKGROUP
server role = standalone server
security = user
map to guest = Never
server min protocol = SMB2_02
server signing = mandatory
ntlm auth = ntlmv2-only
smb ports = $port
interfaces = $interfaces
bind interfaces only = yes
private dir = $STATE/private
state directory = $STATE
cache directory = $STATE/cache
lock directory = $STATE/locks
pid directory = $STATE/run
ncalrpc dir = $STATE/ncalrpc
log file = $STATE/logs/smb.%m
max log size = 1024
passdb backend = tdbsam:$STATE/private/passdb.tdb
load printers = no
disable spoolss = yes
printcap name = /dev/null
[ROMs]
path = $BASE/roms
browseable = yes
read only = no
guest ok = no
valid users = root
follow symlinks = no
wide links = no
create mask = 0644
directory mask = 0755
[Movies]
path = $BASE/videos
browseable = yes
read only = no
guest ok = no
valid users = root
follow symlinks = no
wide links = no
create mask = 0644
directory mask = 0755
EOF
    if [ -d /home/apps/retroarch/roms ]; then
        cat >>"$CONF.next" <<EOF
[RetroArchROMs]
path = /home/apps/retroarch/roms
browseable = yes
read only = no
guest ok = no
valid users = root
follow symlinks = no
wide links = no
create mask = 0644
directory mask = 0755
EOF
    fi
    sh "$RUN" testparm -s "$CONF.next" >/dev/null
    mv -f "$CONF.next" "$CONF"
    echo "Choose a dedicated SMB password for username root (SSH password is unchanged)."
    if [ -t 0 ]; then sh "$RUN" smbpasswd -c "$CONF" -a root
    else sh "$RUN" smbpasswd -c "$CONF" -s -a root; fi
    touch "$STATE/.configured"
    echo 'Configured: Windows Explorer \\<SteamLink-IP>\ROMs and \\<SteamLink-IP>\Movies'
    ;;
start|enable)
    [ "$(id -u)" = 0 ] || fail "Run as root on the Steam Link"
    [ -f "$STATE/.configured" ] && [ -f "$CONF" ] || fail "Run: sh samba-server.sh configure"
    if ! running; then
        helpers
        sh "$RUN" testparm -s "$CONF" >/dev/null
        sh "$RUN" smbd -s "$CONF" -D
        tries=0
        until running; do
            tries=$((tries+1));[ "$tries" -lt 30 ] || fail "Samba did not start; see samba/logs"
            sleep 0.1
        done
    fi
    [ "$1" != enable ] || touch "$STATE/.enabled"
    echo "RUNNING"
    ;;
stop|disable)
    [ "$1" != disable ] || rm -f "$STATE/.enabled"
    if running; then
        sh "$RUN" smbcontrol -s "$CONF" smbd shutdown
        tries=0
        while running; do
            tries=$((tries+1));[ "$tries" -lt 30 ] || fail "Samba is still stopping"
            sleep 0.1
        done
    fi
    echo "STOPPED"
    ;;
*) fail "Usage: sh samba-server.sh status|check|configure|start|stop|enable|disable";;
esac

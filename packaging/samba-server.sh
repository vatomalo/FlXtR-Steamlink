#!/bin/sh
# Optional ROM-only Samba server. Requires separately built ARMv7 Samba binaries.
set -eu
cd "$(dirname "$0")"
BASE=$(pwd)
SMBD="$BASE/samba/bin/smbd"
PASSWD="$BASE/samba/bin/smbpasswd"
CONF="$BASE/samba/smb.conf"
case "${1:-status}" in
status)
  [ -x "$SMBD" ] && "$SMBD" -V || echo "Samba ARMv7 binaries not installed yet"
  ;;
configure)
  [ "$(id -u)" = 0 ] || { echo "Root required"; exit 2; }
  [ -x "$SMBD" ] && [ -x "$PASSWD" ] || { echo "Missing ARMv7 smbd or smbpasswd in samba/bin"; exit 2; }
  mkdir -p samba/private samba/locks samba/logs samba/run
  chmod 700 samba/private
  cat > "$CONF" <<EOF
[global]
workgroup = WORKGROUP
server role = standalone server
security = user
map to guest = Never
server min protocol = SMB2
smb ports = 445
private dir = $BASE/samba/private
lock directory = $BASE/samba/locks
pid directory = $BASE/samba/run
log file = $BASE/samba/logs/smb.%m
passdb backend = tdbsam:$BASE/samba/private/passdb.tdb
[ROMs]
path = /home/apps/retroarch/roms
browseable = yes
read only = no
guest ok = no
valid users = root
create mask = 0644
directory mask = 0755
EOF
  chmod 600 "$CONF"
  echo "Choose a dedicated Samba password, different from your SSH password."
  "$PASSWD" -c "$CONF" -a root
  ;;
start)
  [ -f "$CONF" ] && [ -x "$SMBD" ] || { echo "Run configure after installing Samba binaries"; exit 2; }
  "$SMBD" -s "$CONF" -D
  echo "Test Windows Explorer: \\\\<SteamLink-IP>\\ROMs"
  ;;
stop)
  CONTROL="$BASE/samba/bin/smbcontrol"
  [ -x "$CONTROL" ] || { echo "smbcontrol binary missing"; exit 2; }
  "$CONTROL" -s "$CONF" smbd shutdown
  ;;
*) echo "Usage: sh samba-server.sh status|configure|start|stop"; exit 2 ;;
esac

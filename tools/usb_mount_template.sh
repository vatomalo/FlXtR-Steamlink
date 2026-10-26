#!/bin/sh
# Steam Link USB layout recovery. Default mode is read-only diagnostics.
# Applies only to the two UUIDs selected and verified during setup.
set -eu
FAT_UUID=@FAT_UUID@
SYSTEM_UUID=@SYSTEM_UUID@
ANCHOR=/mnt/flxtr-system
MODE=${1:---check}
case "$MODE" in --check|--apply) ;; *) echo 'Usage: restore-usb-storage.sh [--check|--apply]' >&2; exit 2;; esac
fail() { echo "Storage restore: $*" >&2; exit 1; }
# Ignore mount records hidden by a later parent mount. st_dev must match
# the filesystem currently visible through the path.
visible_record() {
    visible=$(stat -c %d "$1" 2>/dev/null) || return 0
    awk -v p="$1" -v visible="$visible" -v field="$2" '
    $5==p {
        split($3,d,":"); actual=d[1]*256+(d[2]%256)+int(d[2]/256)*1048576
        if(actual!=visible) next
        for(i=7;i<=NF;i++) if($i=="-") {
            if(field=="device") value=$(i+2)
            if(field=="type") value=$(i+1)
            if(field=="root") value=$4
        }
    } END {print value}' /proc/self/mountinfo
}
top_device() { visible_record "$1" device; }
top_type() { visible_record "$1" type; }
mount_root() { visible_record "$1" root; }
fat=; system=; fat_count=0; system_count=0
for device in /dev/block/sd[a-z][1-9] /dev/block/sd[a-z][1-9][0-9]; do
    [ -b "$device" ] || continue
    description=$(blkid "$device" 2>/dev/null || true)
    case "$description" in *"UUID=\"$FAT_UUID\""*) fat=$device; fat_count=$((fat_count+1));; esac
    case "$description" in *"UUID=\"$SYSTEM_UUID\""*) system=$device; system_count=$((system_count+1));; esac
done
[ "$fat_count" = 1 ] && [ "$system_count" = 1 ] || fail 'Expected unique FAT and Linux USB partitions are not both present; nothing changed.'
echo "FAT data: $fat ($FAT_UUID)"
echo "Linux apps: $system ($SYSTEM_UUID)"
base=
for point in "$ANCHOR" /mnt/disk/.system /mnt/disk; do
    if [ "$(top_device "$point")" = "$system" ] && [ "$(mount_root "$point")" = / ]; then
        base=$point; break
    fi
done
if [ -n "$base" ]; then
    [ -d "$base/apps" ] && [ -d "$base/local" ] || fail 'Linux volume does not contain the expected installation.'
    [ ! -L "$base/apps" ] && [ ! -L "$base/local" ] || fail 'Unexpected symlink in USB layout.'
    echo "Existing Linux volume root: $base"
else
    echo 'Linux partition is not currently mounted at an expected location.'
fi
echo "Current /mnt/disk: $(top_device /mnt/disk) $(top_type /mnt/disk)"
echo "Current /home/apps: $(top_device /home/apps), filesystem root $(mount_root /home/apps)"
echo "Current /usr/local: $(top_device /usr/local), filesystem root $(mount_root /usr/local)"
if [ "$MODE" = --check ]; then
    echo 'Plan: retain a Linux-volume anchor; expose FAT at /mnt/disk; restore .system, apps and local bind mounts.'
    echo 'Read-only check complete. No mount or file was changed.'
    exit 0
fi
[ "$(id -u)" = 0 ] || fail 'Apply requires root.'
mkdir /tmp/flxtr-storage-restore.lock 2>/dev/null || fail 'Another restore may be running; inspect the temporary lock.'
trap 'rmdir /tmp/flxtr-storage-restore.lock 2>/dev/null || true' 0
trap 'exit 1' 1 2 15
if [ -n "$(top_device "$ANCHOR")" ]; then
    [ "$(top_device "$ANCHOR")" = "$system" ] && [ "$(mount_root "$ANCHOR")" = / ] || fail 'Refusing to cover an unrelated anchor mount.'
else
    [ ! -L "$ANCHOR" ] || fail 'Anchor is a symlink.'
    mkdir -p "$ANCHOR"
    [ -z "$(ls -A "$ANCHOR")" ] || fail 'Refusing to hide files in the anchor directory.'
    if [ -n "$base" ]; then mount --bind "$base" "$ANCHOR"; else mount -t ext3 "$system" "$ANCHOR"; fi
fi
[ -d "$ANCHOR/apps" ] && [ -d "$ANCHOR/local" ] || fail 'Expected installation not found at anchor.'
# Remove only this investigation's temporary read-only FAT view before opening
# the normal read/write FAT view. No native/user mount is unmounted here.
if [ "$(top_device /tmp/flxtr-usb-fat-ro)" = "$fat" ]; then
    [ "$(top_type /tmp/flxtr-usb-fat-ro)" = vfat ] || fail 'Unexpected diagnostic mount type.'
    umount /tmp/flxtr-usb-fat-ro
fi
if [ "$(top_device /mnt/disk)" != "$fat" ]; then
    current=$(top_device /mnt/disk)
    [ -z "$current" ] || [ "$current" = "$system" ] || fail 'Refusing to cover an unrelated /mnt/disk mount.'
    mount -t vfat "$fat" /mnt/disk
fi
[ "$(top_type /mnt/disk)" = vfat ] || fail 'Expected FAT filesystem is not visible at /mnt/disk.'
[ -d /mnt/disk/.system ] && [ ! -L /mnt/disk/.system ] || fail 'Expected existing .system directory is missing.'
if [ "$(top_device /mnt/disk/.system)" != "$system" ]; then
    [ -z "$(top_device /mnt/disk/.system)" ] || fail 'Refusing to cover unrelated .system mount.'
    [ -z "$(ls -A /mnt/disk/.system)" ] || fail 'Refusing to hide files in the FAT .system directory.'
    mount --bind "$ANCHOR" /mnt/disk/.system
fi
for part in apps local; do
    case "$part" in apps) target=/home/apps;; local) target=/usr/local;; esac
    [ ! -L "$target" ] || fail 'Unexpected symlink at bind target.'
    if [ "$(top_device "$target")" = "$system" ] && [ "$(mount_root "$target")" = "/$part" ]; then
        continue
    fi
    current=$(top_device "$target")
    case "$part:$current" in apps:/dev/loop0|local:unionfs|apps:|local:) ;; *) fail "Refusing to cover unexpected mount at $target.";; esac
    mount --bind "$ANCHOR/$part" "$target"
done
# Bind stable volume identities, then link ROM folders without replacing files.
DATA_ALIAS=/mnt/flxtr-usb/$FAT_UUID/data
SYSTEM_ALIAS=/mnt/flxtr-usb/$SYSTEM_UUID/system
for pair in data system; do
    case "$pair" in data) source=/mnt/disk; alias=$DATA_ALIAS;; system) source=$ANCHOR; alias=$SYSTEM_ALIAS;; esac
    if [ -z "$(top_device "$alias")" ]; then
        [ ! -L "$alias" ] || fail 'Unexpected symlink at stable mount path.'
        mkdir -p "$alias"
        [ -z "$(ls -A "$alias")" ] || fail 'Refusing to hide stable-path files.'
        mount --bind "$source" "$alias"
    else
        [ "$(top_device "$alias")" = "$(top_device "$source")" ] || fail 'Stable volume alias points to another disk.'
    fi
done
safe_link() {
    if [ -L "$2" ]; then [ "$(readlink "$2")" = "$1" ] || fail 'Conflicting existing symbolic link.'
    elif [ -e "$2" ]; then fail 'Refusing to replace an existing file with a symbolic link.'
    else ln -s "$1" "$2"; fi
}
mkdir -p /mnt/flxtr-usb/by-uuid
safe_link "$DATA_ALIAS" /mnt/flxtr-usb/by-uuid/$FAT_UUID
safe_link "$SYSTEM_ALIAS" /mnt/flxtr-usb/by-uuid/$SYSTEM_UUID
if [ -d /home/apps/@APP_NAME@/roms ] && [ -d "$DATA_ALIAS/roms" ]; then
    safe_link "$DATA_ALIAS/roms" /home/apps/@APP_NAME@/roms/USB-$FAT_UUID
fi
ldconfig
echo 'USB data, apps and local-library mounts restored. No content was deleted or moved.'
df -k /mnt/disk /home/apps /usr/local

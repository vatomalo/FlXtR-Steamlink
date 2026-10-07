#!/bin/sh
# Disconnect only Bluetooth DualShock 4 devices after the shell's idle timer.
# Never powers down the adapter, unpairs devices, or touches keyboards.
set -eu
for input in /sys/class/input/js*/device; do
    [ -r "$input/id/bustype" ] || continue
    [ "$(cat "$input/id/bustype")" = 0005 ] || continue
    [ "$(cat "$input/id/vendor")" = 054c ] || continue
    case "$(cat "$input/id/product")" in 05c4|09cc) ;; *) continue;; esac
    address=$(cat "$input/uniq" | tr 'a-f' 'A-F')
    case "$address" in *[!0-9A-F:]*|'') continue;; esac
    [ "${#address}" -eq 17 ] || continue
    adapter_address=$(cat "$input/phys")
    for adapter in /sys/class/bluetooth/hci[0-9]*; do
        [ -r "$adapter/address" ] || continue
        [ "$(cat "$adapter/address")" = "$adapter_address" ] || continue
        device_path="/org/bluez/${adapter##*/}/dev_$(echo "$address" | tr ':' '_')"
        echo "Idle DS4 disconnect: $address"
        if [ "${1:-}" != --dry-run ]; then
            dbus-send --system --print-reply --reply-timeout=3000 --dest=org.bluez \
                "$device_path" org.bluez.Device1.Disconnect
        fi
    done
done

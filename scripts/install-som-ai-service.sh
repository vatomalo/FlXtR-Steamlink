#!/usr/bin/env bash
set -euo pipefail

repo=$(cd "$(dirname "$0")/.." && pwd)
case "$repo" in
  *$'\n'*|*' '*)
    echo "The checkout path must not contain spaces or newlines for the systemd unit." >&2
    exit 1
    ;;
esac

command -v python3 >/dev/null || { echo "python3 is required" >&2; exit 1; }
command -v ssh >/dev/null || { echo "ssh is required" >&2; exit 1; }
command -v systemctl >/dev/null || { echo "systemd is required" >&2; exit 1; }

config_dir=${XDG_CONFIG_HOME:-"$HOME/.config"}/flxtr
unit_dir=${XDG_CONFIG_HOME:-"$HOME/.config"}/systemd/user
mkdir -p "$config_dir" "$unit_dir"

if [[ ! -e "$config_dir/som-ai.env" ]]; then
  install -m 0600 "$repo/packaging/som-ai.env.example" "$config_dir/som-ai.env"
  echo "Created $config_dir/som-ai.env"
else
  echo "Preserved existing $config_dir/som-ai.env"
fi

escaped_repo=${repo//&/\\&}
sed "s&@REPO@&$escaped_repo&g" "$repo/packaging/flxtr-som-ai.service.in" \
  > "$unit_dir/flxtr-som-ai.service"
chmod 0644 "$unit_dir/flxtr-som-ai.service"

systemctl --user daemon-reload
systemctl --user enable --now flxtr-som-ai.service

echo "Installed and started flxtr-som-ai.service"
echo "Enable boot without login once with: sudo loginctl enable-linger $USER"
echo "Logs: journalctl --user -u flxtr-som-ai.service -f"

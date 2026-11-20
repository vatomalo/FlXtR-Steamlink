#!/usr/bin/env bash
# Read-only snapshot of the Secret of Mana Ultimate install on the Steam Link.
#
# Copies (never modifies) the proxy core, the SNES core, launcher pieces, AI
# feature files and the save files into a new timestamped folder OUTSIDE Git,
# records remote and local SHA-256 for every file, and the ROM by checksum
# only (the ROM itself is never copied). Refuses to reuse an existing folder.
#
#   bash scripts/som-backup-steamlink.sh [user@host] [backup-root]
#
# Defaults: root@100.124.74.13, ~/backups/som-steamlink. Restore steps are
# written into the backup as RESTORE.txt (see also docs/SOM-RELEASE-CHECKLIST.md).
set -euo pipefail
host=${1:-root@100.124.74.13}
root=${2:-$HOME/backups/som-steamlink}
stamp=$(date +%Y%m%d-%H%M%S)
dest=$root/$stamp
if [ -e "$dest" ]; then echo "refusing: $dest exists" >&2; exit 1; fi
mkdir -p "$dest/files"

E=/home/apps/retroarch/cores/experimental
R='/home/apps/retroarch/roms/Snes'
G=/home/apps/greenlink
files=(
  "$E/som_ai_proxy_libretro.so"
  "$E/som_ai_disciplines.party-1" "$E/som_ai_disciplines.party-2"
  "$E/som_cartwheel_frames.bin" "$E/som_weapon_masks.bin"
  "$E/snes9x2005_plus_flxtr_libretro.so"
  "$E/som_ai_disciplines" "$E/som_ai_gambits" "$E/som_ai_combat"
  "$E/som_music_genre" "$E/som_song_names.tsv" "$E/som_ai_rom_patch.txt"
  "$E/som_summon.bin" "$E/som_ai_living_world_enable" "$E/som_ai_summon_anyone"
  "$E/som_ai_idle_autostart_disable"
  "$R/Secret of Mana, Turbo.srm" "$R/Secret of Mana, Turbo.state.auto"
  "$G/greenlink.sh" "$G/tools/som_music_bridge.py" "$G/tools/state_guard.py"
  "$G/game-runtime.cfg"
)
ssh_q() { ssh -o BatchMode=yes -o StrictHostKeyChecking=yes -o ConnectTimeout=10 "$host" "$@"; }

{
  echo "# Secret of Mana Ultimate Steam Link snapshot $stamp"
  echo "# host $host; remote sha256, local sha256, mode, path"
  echo "# running: $(ssh_q 'ps | grep -c "[r]etroarch.exec"') RetroArch process(es)"
  echo "# ROM (not copied): $(ssh_q "sha256sum '$R/Secret of Mana, Turbo.smc'; wc -c <'$R/Secret of Mana, Turbo.smc'" | tr '\n' ' ')"
} >"$dest/MANIFEST.txt"

for path in "${files[@]}"; do
  if ! ssh_q "test -e '$path'"; then
    echo "absent - - $path" >>"$dest/MANIFEST.txt"; continue
  fi
  local_name=$(printf '%s' "$path" | sed 's#^/##; s#/#__#g; s# #_#g')
  ssh_q "cat '$path'" >"$dest/files/$local_name"
  read -r remote_sum mode mtime < <(ssh_q "printf '%s %s %s\n' \$(sha256sum '$path' | cut -d' ' -f1) \$(stat -c '%a %Y' '$path')")
  chmod "$mode" "$dest/files/$local_name"; touch -d "@$mtime" "$dest/files/$local_name"
  local_sum=$(sha256sum "$dest/files/$local_name" | cut -d' ' -f1)
  [ "$remote_sum" = "$local_sum" ] || { echo "checksum mismatch: $path" >&2; exit 1; }
  echo "$remote_sum $local_sum $mode $path -> files/$local_name" >>"$dest/MANIFEST.txt"
done
(cd "$dest/files" && sha256sum -- * >../SHA256SUMS)

cat >"$dest/RESTORE.txt" <<EOF
Restore one file (game CLOSED first: ps on the Steam Link must show no retroarch.exec):
  ssh $host 'cat >"<original path>.restore"' <"files/<name>" &&
  ssh $host 'chmod <mode> "<original path>.restore" && mv "<original path>.restore" "<original path>"'
(the rename keeps a running process on its old copy; check the sha256 in MANIFEST.txt after).
Saves: copy BOTH Secret of Mana, Turbo.srm and .state.auto from the same snapshot.
Never restore a save while the game runs: RetroArch writes them on exit.
EOF
chmod -R a-w "$dest"
echo "snapshot: $dest"

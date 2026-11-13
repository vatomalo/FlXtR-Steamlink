#!/usr/bin/env bash
# Keeps Potos villagers on their daily schedules while the game runs.
# NPC walks still need the Steam Link enable marker
# /home/apps/retroarch/cores/experimental/som_ai_living_world_enable.
set -euo pipefail

repo=$(cd "$(dirname "$0")/.." && pwd)
host=${SOM_AI_HOST:-root@100.124.74.13}
# Every read moves ~39 KB over the ~210 KB/s link; keep the AI's share large.
interval=${SOM_LW_INTERVAL:-2.0}
args=(run --host "$host" --interval "$interval" --steps 0)
[ "${SOM_LW_LIVE:-1}" != 0 ] && args+=(--live)
if [ "${SOM_LW_DIRECTOR:-0}" != 0 ]; then
  args+=(--director --ollama-url "${SOM_AI_OLLAMA_URL:-http://127.0.0.1:11434}"
         --model "${SOM_AI_MODEL:-qwen3:0.6b}")
fi

exec python3 "$repo/tools/som_living_world.py" "${args[@]}"

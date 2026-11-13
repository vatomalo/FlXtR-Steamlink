#!/usr/bin/env bash
set -euo pipefail

repo=$(cd "$(dirname "$0")/.." && pwd)
state_dir=${XDG_STATE_HOME:-"$HOME/.local/state"}/flxtr
mkdir -p "$state_dir"

host=${SOM_AI_HOST:-root@100.124.74.13}
model=${SOM_AI_MODEL:-qwen3:0.6b}
policy=${SOM_AI_POLICY:-hybrid}
objective=${SOM_AI_OBJECTIVE:-Explore safely using verified tiles and attack when appropriate}
ollama_url=${SOM_AI_OLLAMA_URL:-http://127.0.0.1:11434}
interval=${SOM_AI_INTERVAL:-0.1}
start_timeout=${SOM_AI_START_TIMEOUT:-3600}
history_db=${SOM_AI_HISTORY_DB:-"$state_dir/som-ai.sqlite"}

exec python3 "$repo/tools/som_qwen_bridge.py" \
  --host "$host" \
  --ollama-url "$ollama_url" \
  --model "$model" \
  --policy "$policy" \
  --objective "$objective" \
  --interval "$interval" \
  --steps 0 \
  --start-timeout "$start_timeout" \
  --history-db "$history_db" \
  --wram-remote \
  --ring-control

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
hunt_direction=${SOM_AI_HUNT_DIRECTION:-west}
shop_gold=${SOM_AI_SHOP_GOLD:-500}
gp_reward=${SOM_AI_GP_REWARD:-1}
hp_penalty=${SOM_AI_HP_PENALTY:-2}
# Deterministic hunt -> shop -> inn -> save loop; SOM_AI_PLAY_LOOP=0 restores Qwen goals.
extra_args=()
# Guard/counter learning; off by default because it cost swings (see bridge).
[ "${SOM_AI_BLOCK:-0}" != 0 ] && extra_args+=(--block-learning)
loop_args=()
transport_args=(--wram-remote --ring-control)
if [ "${SOM_AI_ANDROID:-0}" != 0 ]; then
  transport_args+=(--android-listen "${SOM_AI_ANDROID_LISTEN:-100.69.7.81:43928}")
fi
if [ "${SOM_AI_PLAY_LOOP:-1}" != 0 ]; then
  loop_args=(--play-loop --trip-gold "${SOM_AI_TRIP_GOLD:-100}")
  [ -n "${SOM_AI_HUNT_MAP:-}" ] && loop_args+=(--hunt-map "$SOM_AI_HUNT_MAP")
fi

exec python3 "$repo/tools/som_qwen_bridge.py" \
  --host "$host" \
  --ollama-url "$ollama_url" \
  --model "$model" \
  --policy "$policy" \
  --hunt-direction "$hunt_direction" \
  --shop-gold "$shop_gold" \
  --gp-reward "$gp_reward" \
  --hp-penalty "$hp_penalty" \
  --objective "$objective" \
  --interval "$interval" \
  --steps 0 \
  --start-timeout "$start_timeout" \
  --history-db "$history_db" \
  "${transport_args[@]}" \
  "${loop_args[@]}" "${extra_args[@]}"

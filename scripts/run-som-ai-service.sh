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
loop_args=()
if [ "${SOM_AI_PLAY_LOOP:-1}" != 0 ]; then
  loop_args=(--play-loop --trip-gold "${SOM_AI_TRIP_GOLD:-100}")
  [ -n "${SOM_AI_HUNT_MAP:-}" ] && loop_args+=(--hunt-map "$SOM_AI_HUNT_MAP")
fi
bridge_args=("$repo/tools/som_qwen_bridge.py" \
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
  --ring-control \
  "${loop_args[@]}")

if [ -z "${SOM_AI_HTTP_ENDPOINT:-}" ]; then
  exec python3 "${bridge_args[@]}" --history-db "$history_db" --wram-remote
fi

# With an Android endpoint configured, keep independent planners alive for
# both devices. Stable world knowledge is shared through SQLite WAL, while
# each device namespaces transient goals and combat/session state.

worker(){
  local label=$1 database=$2 child= status
  shift 2
  stop_worker(){
    if [ -n "$child" ]; then kill "$child" 2>/dev/null || true; wait "$child" 2>/dev/null || true; fi
    exit 0
  }
  trap stop_worker TERM INT
  while true; do
    printf '[service] starting %s planner\n' "$label"
    python3 "${bridge_args[@]}" --history-db "$database" "$@" & child=$!
    if wait "$child"; then status=0; else status=$?; fi
    child=
    printf '[service] %s planner exited (%s); restarting in 5 seconds\n' "$label" "$status" >&2
    sleep 5
  done
}

workers=()
worker "Steam Link" "$history_db" --agent-namespace steamlink --wram-remote & workers+=("$!")
worker "Android" "$history_db" --agent-namespace android --boss-dir "$state_dir/som-boss-android" --http-endpoint "$SOM_AI_HTTP_ENDPOINT" & workers+=("$!")
cleanup(){ kill "${workers[@]}" 2>/dev/null || true; wait "${workers[@]}" 2>/dev/null || true; }
trap 'exit 0' TERM INT
trap cleanup EXIT
wait

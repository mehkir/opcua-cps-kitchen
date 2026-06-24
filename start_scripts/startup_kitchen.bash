#!/usr/bin/bash

set -Eeuo pipefail

# Initialize variables
robots_count=""
evaluate_orders=0

# Parse flags
while getopts ":r:e" opt; do
  case "$opt" in
    r) robots_count="$OPTARG" ;;
    e) evaluate_orders=1 ;;
    *) echo "Usage: $0 -r <robots_count> [-e]" ; exit 1 ;;
  esac
done
shift $((OPTIND - 1))

# Validate robots_count was provided
if [[ -z "$robots_count" ]]; then
  echo "Usage: $0 -r <robots_count> [-e]"
  exit 1
fi

if (( robots_count < 1 )); then
    echo "robots count must be >= 1"
    exit 1
fi

pids=()

start_bg() {
    "$@" &
    pids+=("$!")
}

cleanup() {
    trap - INT TERM EXIT

    echo "Stopping ${#pids[@]} processes..."

    for pid in "${pids[@]}"; do
        kill -TERM "$pid" 2>/dev/null || true
    done

    local deadline=$((SECONDS + 5))
    for pid in "${pids[@]}"; do
        while kill -0 "$pid" 2>/dev/null && (( SECONDS < deadline )); do
            sleep 0.1
        done
    done

    for pid in "${pids[@]}"; do
        if kill -0 "$pid" 2>/dev/null; then
            echo "Force killing $pid"
            kill -KILL "$pid" 2>/dev/null || true
        fi
    done

    wait 2>/dev/null || true
}

trap cleanup INT TERM EXIT

SCRIPT_PATH="$(realpath "$0")"
SCRIPT_DIR="$(dirname "$SCRIPT_PATH")"
PROJECT_DIRECTORY="$(cd -- "$SCRIPT_DIR/.." && pwd)"
"$PROJECT_DIRECTORY/build.bash"
ROBOTS_COUNT=$robots_count
CONVEYOR_SIZE=$(( ROBOTS_COUNT + 1 ))

if (( evaluate_orders > 0 )); then
    start_bg "$PROJECT_DIRECTORY/build/start_event_collector"
fi
start_bg "$PROJECT_DIRECTORY/build/discovery/discovery_server"
start_bg "$PROJECT_DIRECTORY/start_scripts/start_controller.bash"
start_bg "$PROJECT_DIRECTORY/start_scripts/start_conveyor.bash" "$ROBOTS_COUNT"
start_bg "$PROJECT_DIRECTORY/start_scripts/start_robots.bash" "$ROBOTS_COUNT" "$CONVEYOR_SIZE"
start_bg "$PROJECT_DIRECTORY/start_scripts/start_kitchen.bash" "$ROBOTS_COUNT"

wait

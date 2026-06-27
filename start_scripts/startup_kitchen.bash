#!/usr/bin/bash

set -Eeuo pipefail

# Initialize variables
robots_count=""
evaluate_orders=0

skip_build=0

# Parse flags
while getopts ":r:en" opt; do
  case "$opt" in
    r) robots_count="$OPTARG" ;;
    e) evaluate_orders=1 ;;
    n) skip_build=1 ;;
    *) echo "Usage: $0 -r <robots_count> [-e] [-n]" ; exit 1 ;;
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
event_collector_pid=""

start_bg() {
    "$@" &
    pids+=("$!")
}

cleanup() {
    trap - INT TERM EXIT

    echo "Stopping ${#pids[@]} regular processes..."

    if [[ -n "${event_collector_pid:-}" ]]; then
        echo "Stopping event collector $event_collector_pid..."
        kill -TERM "$event_collector_pid" 2>/dev/null || true
    fi

    for pid in "${pids[@]}"; do
        kill -TERM "$pid" 2>/dev/null || true
    done

    local deadline=$((SECONDS + 10))
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


    if [[ -n "${event_collector_pid:-}" ]]; then
        echo "Waiting for event collector $event_collector_pid to write evaluation CSV files..."

        local event_deadline=$((SECONDS + 120))
        while kill -0 "$event_collector_pid" 2>/dev/null && (( SECONDS < event_deadline )); do
            sleep 0.5
        done

        if kill -0 "$event_collector_pid" 2>/dev/null; then
            echo "Warning: event collector $event_collector_pid is still running after timeout. Not force killing it."
        fi
    fi

    wait 2>/dev/null || true
}

trap cleanup INT TERM EXIT

SCRIPT_PATH="$(realpath "$0")"
SCRIPT_DIR="$(dirname "$SCRIPT_PATH")"
PROJECT_DIRECTORY="$(cd -- "$SCRIPT_DIR/.." && pwd)"
ROBOTS_COUNT=$robots_count
CONVEYOR_SIZE=$(( ROBOTS_COUNT + 1 ))

if (( skip_build == 0 )); then
    "$PROJECT_DIRECTORY/build.bash"
else
    echo "Skipping build."
fi

if (( evaluate_orders > 0 )); then
    "$PROJECT_DIRECTORY/build/start_event_collector" &
    event_collector_pid="$!"
fi
start_bg "$PROJECT_DIRECTORY/build/discovery/discovery_server"
start_bg "$PROJECT_DIRECTORY/start_scripts/start_controller.bash"
start_bg "$PROJECT_DIRECTORY/start_scripts/start_conveyor.bash" "$ROBOTS_COUNT"
start_bg "$PROJECT_DIRECTORY/start_scripts/start_robots.bash" "$ROBOTS_COUNT" "$CONVEYOR_SIZE"
start_bg "$PROJECT_DIRECTORY/start_scripts/start_kitchen.bash" "$ROBOTS_COUNT"

wait

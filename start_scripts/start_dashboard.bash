#!/usr/bin/env bash
set -Eeuo pipefail

# Initialize variables
robots_count=""
placing_rate=""
backend_pid=""
http_pid=""

# Parse flags
while getopts ":r:" opt; do
    case "$opt" in
        r) robots_count="$OPTARG" ;;
        *) echo "Usage: $0 -r <robots_count>" ; exit 1 ;;
    esac
done
shift $((OPTIND - 1))

# Validate mandatory flags
if [[ -z "$robots_count" ]]; then
    echo "Usage: $0 -r <robots_count>"
    exit 1
fi

# Validate numeric values
if ! [[ "$robots_count" =~ ^[0-9]+$ ]] || (( robots_count < 1 )); then
        echo "robots count must be >= 1"
        exit 1
fi

SCRIPT_PATH="$(realpath "$0")"
SCRIPT_DIR="$(dirname "$SCRIPT_PATH")"
PROJECT_DIRECTORY="$(cd -- "$SCRIPT_DIR/.." && pwd)"

# Load placing_rate from timing_config.json
placing_rate=$(jq -r '.placing_rate' "$PROJECT_DIRECTORY/timing_config.json")
if [[ -z "$placing_rate" ]]; then
    echo "Error: placing_rate not found in timing_config.json"
    exit 1
fi

if ! [[ "$placing_rate" =~ ^[0-9]+$ ]] || (( placing_rate < 1 )); then
        echo "placing rate must be >= 1"
        exit 1
fi

ROBOTS_COUNT=$robots_count
PLACING_RATE=$placing_rate

cd "$PROJECT_DIRECTORY/cps-kitchen-dashboard"
# Preserve existing LD_LIBRARY_PATH while prepending our library dir
LIB_DIR="$(pwd)/my-addons/open62541/lib"
export LD_LIBRARY_PATH="$LIB_DIR${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

node backend.js --robot-count "$ROBOTS_COUNT" --placing-rate "$PLACING_RATE" &
backend_pid=$!

python3 -m http.server 8000 &
http_pid=$!

cleanup() {
    trap - INT TERM EXIT

    echo "Stopping dashboard..."

    for pid in "${backend_pid:-}" "${http_pid:-}"; do
        if [[ -n "$pid" ]]; then
            kill -TERM "$pid" 2>/dev/null || true
        fi
    done

    local deadline=$((SECONDS + 3))
    for pid in "${backend_pid:-}" "${http_pid:-}"; do
        if [[ -z "$pid" ]]; then
            continue
        fi

        while kill -0 "$pid" 2>/dev/null && (( SECONDS < deadline )); do
            sleep 0.1
        done
    done

    for pid in "${backend_pid:-}" "${http_pid:-}"; do
        if [[ -n "$pid" ]] && kill -0 "$pid" 2>/dev/null; then
            echo "Force killing dashboard process $pid"
            kill -KILL "$pid" 2>/dev/null || true
        fi
    done

    wait 2>/dev/null || true
}

trap cleanup INT TERM EXIT
wait -n "$backend_pid" "$http_pid" || true
cleanup

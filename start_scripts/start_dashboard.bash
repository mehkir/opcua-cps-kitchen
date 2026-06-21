#!/usr/bin/bash

# Initialize variables
robots_count=""
placing_rate=""

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

# Define a cleanup function
kill_http_server_and_backend() {
    for port in 8000; do
        pids="$(lsof -t -iTCP:$port -sTCP:LISTEN)"
        if [ -n "$pids" ]; then
            kill $pids 2>/dev/null || true
        else
            echo "No process found using port $port"
        fi
    done
    exit 0
}

# Trap SIGINT (Ctrl+C)
trap kill_http_server_and_backend SIGINT

cd "$PROJECT_DIRECTORY/cps-kitchen-dashboard"
# Preserve existing LD_LIBRARY_PATH while prepending our library dir
LIB_DIR="$(pwd)/my-addons/open62541/lib"
export LD_LIBRARY_PATH="$LIB_DIR${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
npm start -- --robot-count $ROBOTS_COUNT --placing-rate $PLACING_RATE &
sleep 1
python3 -m http.server 8000 &
# Wait for all background processes to finish
wait

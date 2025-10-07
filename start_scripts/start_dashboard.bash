#!/usr/bin/bash
# Validate argument
if (( $# < 1 )); then
  echo "Usage: $0 <robots_count>"
  exit 1
fi

if (( $1 < 1)); then
    echo "robots count must be >= 1"
fi

SCRIPT_PATH="$(realpath "$0")"
SCRIPT_DIR="$(dirname "$SCRIPT_PATH")"
cd -- "$SCRIPT_DIR"
cd ..
PROJECT_DIRECTORY="$(pwd)"
ROBOTS_COUNT=$1

# Define a cleanup function
kill_http_server_and_backend() {
    kill $(lsof -t -iTCP:8000 -sTCP:LISTEN)
    kill $(lsof -t -iTCP:8080 -sTCP:LISTEN)
    exit 0
}

# Trap SIGINT (Ctrl+C)
trap kill_http_server_and_backend SIGINT

cd "$PROJECT_DIRECTORY/cps-kitchen-dashboard"
# Preserve existing LD_LIBRARY_PATH while prepending our library dir
LIB_DIR="$(pwd)/my-addons/open62541/lib"
export LD_LIBRARY_PATH="$LIB_DIR${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
node backend.js --robot-count $ROBOTS_COUNT &
sleep 1
python3 -m http.server 8000 &
# Wait for all background processes to finish
wait

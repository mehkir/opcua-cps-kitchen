#!/usr/bin/bash
# Validate argument
if (( $# < 1 )); then
  echo "Usage: $0 <robots_count>"
  exit 1
fi

if (( $1 < 1)); then
    echo "robots count must be >= 1"
fi

PROJECT_DIRECTORY=/home/mehmet/vscode-workspaces/opcua-cps-kitchen
$PROJECT_DIRECTORY/build.bash
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
export LD_LIBRARY_PATH="$(pwd)/my-addons/open62541/lib"
node backend.js --robot-count $ROBOTS_COUNT &
sleep 1
python3 -m http.server 8000 &
# Wait for all background processes to finish
wait
echo "Dashboard startup completed successfully."
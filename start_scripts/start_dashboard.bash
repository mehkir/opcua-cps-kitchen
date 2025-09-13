#!/usr/bin/bash
PROJECT_DIRECTORY=/home/mehmet/vscode-workspaces/opcua-cps-kitchen
$PROJECT_DIRECTORY/build.bash
ROBOTS_COUNT=4

cd "$PROJECT_DIRECTORY/cps-kitchen-dashboard"
export LD_LIBRARY_PATH="$(pwd)/my-addons/open62541/lib"
node backend.js --robot-count $ROBOTS_COUNT &
sleep 1
python3 -m http.server 8000 &
# Wait for all background processes to finish
wait
kill $(lsof -t -iTCP:8000 -sTCP:LISTEN)
echo "Dashboard startup completed successfully."
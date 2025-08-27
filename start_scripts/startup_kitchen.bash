#!/usr/bin/bash
PROJECT_DIRECTORY=/home/mehmet/vscode-workspaces/opcua-cps-kitchen
$PROJECT_DIRECTORY/build.bash

$PROJECT_DIRECTORY/build/demos/discovery_server &
sleep 1
$PROJECT_DIRECTORY/start_scripts/start_controller.bash &
sleep 1
$PROJECT_DIRECTORY/start_scripts/start_conveyor.bash 4 &
sleep 1
$PROJECT_DIRECTORY/start_scripts/start_robot.bash 4 &
sleep 1
cd "$PROJECT_DIRECTORY/cps-kitchen-dashboard"
export LD_LIBRARY_PATH="$(pwd)/my-addons/open62541/lib"
node backend.js --robot-count 4 &
sleep 1
python3 -m http.server 8000 &
# Wait for all background processes to finish
wait
echo "All startup scripts completed successfully."

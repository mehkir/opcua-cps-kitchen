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

$PROJECT_DIRECTORY/build/demos/discovery_server &
sleep 1
$PROJECT_DIRECTORY/start_scripts/start_controller.bash &
sleep 1
$PROJECT_DIRECTORY/start_scripts/start_conveyor.bash $ROBOTS_COUNT &
sleep 1
$PROJECT_DIRECTORY/start_scripts/start_robot.bash $ROBOTS_COUNT &
sleep 1
$PROJECT_DIRECTORY/start_scripts/start_kitchen.bash $ROBOTS_COUNT &
# Wait for all background processes to finish
wait
echo "All startup scripts closed successfully."

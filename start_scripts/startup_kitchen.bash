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
$PROJECT_DIRECTORY/build.bash
ROBOTS_COUNT=$1
CONVEYOR_SIZE=$(( ROBOTS_COUNT + 1 ))

# Define a cleanup function
kill_kitchen() {
    echo "Waiting 7 seconds for agents to shutdown. The rest will be killed after."
    sleep 7
    for p in start_r start_c discov start_k; do
        pkill -SIGKILL "$p"
    done
    exit 0
}

# Trap SIGINT (Ctrl+C)
trap kill_kitchen SIGINT

$PROJECT_DIRECTORY/build/demos/discovery_server &
sleep 1
$PROJECT_DIRECTORY/start_scripts/start_controller.bash &
sleep 1
$PROJECT_DIRECTORY/start_scripts/start_conveyor.bash $ROBOTS_COUNT &
sleep 1
$PROJECT_DIRECTORY/start_scripts/start_robots.bash $ROBOTS_COUNT $CONVEYOR_SIZE &
sleep 1
$PROJECT_DIRECTORY/start_scripts/start_kitchen.bash $ROBOTS_COUNT &
# Wait for all background processes to finish
wait

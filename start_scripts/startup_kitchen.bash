#!/usr/bin/bash

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

SCRIPT_PATH="$(realpath "$0")"
SCRIPT_DIR="$(dirname "$SCRIPT_PATH")"
PROJECT_DIRECTORY="$(cd -- "$SCRIPT_DIR/.." && pwd)"
$PROJECT_DIRECTORY/build.bash
ROBOTS_COUNT=$robots_count
CONVEYOR_SIZE=$(( ROBOTS_COUNT + 1 ))

# Define a cleanup function
kill_kitchen() {
    echo "5 seconds timeout for agents to shutdown. Still running processes after timeout will be killed."
    sleep 5
    for p in start_r start_c discov start_k; do
        pkill -SIGKILL "$p"
    done
    exit 0
}

# Trap SIGINT (Ctrl+C)
trap kill_kitchen SIGINT

if (( evaluate_orders > 0 )); then
    $PROJECT_DIRECTORY/build/start_event_collector &
    sleep 1
fi
$PROJECT_DIRECTORY/build/discovery/discovery_server &
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

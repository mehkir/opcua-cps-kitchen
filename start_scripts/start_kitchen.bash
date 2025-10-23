#!/usr/bin/bash
ROBOTS=$1
if [[ $ROBOTS -lt 1 ]]; then
    echo "Usage: $0 <number_of_robots>"
    exit 1
fi
if [[ -z $ROBOTS ]]; then
    echo "Error: Number of robots not specified."
    exit 1
fi

SCRIPT_PATH="$(realpath "$0")"
SCRIPT_DIR="$(dirname "$SCRIPT_PATH")"
cd -- "$SCRIPT_DIR"
cd ..
PROJECT_DIRECTORY="$(pwd)"
$PROJECT_DIRECTORY/build/start_kitchen_instance $ROBOTS &
# "$PROJECT_DIRECTORY/build/start_kitchen_instance" "$ROBOTS" >./logs/kitchen_${ROBOTS}_$(date +%Y%m%d%H%M%S) &
exit_code=$?
if [ $exit_code -ne 0 ]; then
    echo "Error: Non-zero exit code detected during kitchen startup. Exiting."
    exit $exit_code
fi      
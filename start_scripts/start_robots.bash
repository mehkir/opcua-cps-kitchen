#!/usr/bin/bash
if [ "$#" -lt 1 ]; then
    echo "Usage: $0 <number_of_robots>"
    exit 1
fi
ROBOTS=$1
if [[ $ROBOTS -lt 1 ]]; then
    echo "Number of robots must be >= 1"
    exit 1
fi
declare -A position_capabilities=(
    [1]="r1.json"
    [2]="r2.json"
    [3]="r3.json"
    [4]="r4.json"
)
SCRIPT_PATH="$(realpath "$0")"
SCRIPT_DIR="$(dirname "$SCRIPT_PATH")"
cd -- "$SCRIPT_DIR"
cd ..
PROJECT_DIRECTORY="$(pwd)"
for ((robot_count = 0; robot_count < ROBOTS; robot_count++)); do
    robot_position=$(( $robot_count + 1 ))
    echo "Starting robot at position $robot_position"
    if [[ ! -v position_capabilities[$robot_position] ]]; then
        echo "No capabilities file mapped for position $robot_position" >&2
        continue
    fi
    "$PROJECT_DIRECTORY/build/start_robot_instance" "$robot_position" "${position_capabilities[$robot_position]}" &
    # $PROJECT_DIRECTORY/build/start_robot_instance $robot_position 1>/dev/null &
    # $PROJECT_DIRECTORY/build/start_robot_instance $robot_position >./logs/robot_${robot_position}_${ROBOTS}_$(date +%Y%m%d%H%M%S) &
    exit_code=$?
    if [ $exit_code -ne 0 ]; then
        echo "Error: Non-zero exit code detected during robot startup. Exiting."
        exit $exit_code
    fi
done
echo "All robots started"
wait
echo "All robots terminated"

#!/usr/bin/bash
if (( $# < 2 )); then
    echo "Usage: $0 <number_of_robots> <conveyor_size>"
    exit 1
fi
if (( $1 < 1)); then
    echo "robots count must be >= 1"
    exit 1
fi
ROBOTS=$1
if (( $2 < 2 )); then
    echo "Coveyor size must be >= 2"
    exit 1
fi
CONVEYOR_SIZE=$2

declare -A position_capabilities

SCRIPT_PATH="$(realpath "$0")"
SCRIPT_DIR="$(dirname "$SCRIPT_PATH")"
PROJECT_DIRECTORY="$(cd -- "$SCRIPT_DIR/.." && pwd)"

while IFS=$'\t' read -r key value; do
    position_capabilities["$key"]="$value"
done < <(jq -r 'to_entries[] | [.key, .value] | @tsv' "$PROJECT_DIRECTORY/robot_config_mapping.json")

pids=()

cleanup() {
    trap - INT TERM EXIT
    kill -TERM "${pids[@]}" 2>/dev/null || true
    sleep 5
    kill -KILL "${pids[@]}" 2>/dev/null || true
    wait 2>/dev/null || true
}

trap cleanup INT TERM EXIT

for ((robot_count = 0; robot_count < ROBOTS; robot_count++)); do
    robot_position=$(( $robot_count + 1 ))
    echo "Starting robot at position $robot_position"
    if [[ ! -v position_capabilities[$robot_position] ]]; then
        echo "No capabilities file mapped for position $robot_position" >&2
        continue
    fi
    "$PROJECT_DIRECTORY/build/start_robot_instance" "$robot_position" "${position_capabilities[$robot_position]}" "$CONVEYOR_SIZE" &
    # "$PROJECT_DIRECTORY/build/start_robot_instance" "$robot_position" "${position_capabilities[$robot_position]}" "$CONVEYOR_SIZE" 1>/dev/null &
    # "$PROJECT_DIRECTORY/build/start_robot_instance" "$robot_position" "${position_capabilities[$robot_position]}" "$CONVEYOR_SIZE" >"$PROJECT_DIRECTORY/logs/robot_${robot_position}_${ROBOTS}_$(date +%Y%m%d%H%M%S)" &
    pids+=("$!")
done
echo "All robots started"
wait "${pids[@]}"
echo "All robots terminated"

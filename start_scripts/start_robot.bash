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
PROJECT_DIRECTORY=/home/mehmet/vscode-workspaces/opcua-cps-kitchen
for ((robot_count = 0; robot_count < ROBOTS; robot_count++)); do
    robot_position=$(( $robot_count + 1 ))
    echo "Starting robot at position $robot_position"
    $PROJECT_DIRECTORY/build/start_robot_instance $robot_position &
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

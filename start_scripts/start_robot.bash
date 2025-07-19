#!/usr/bin/bash
PROJECT_DIRECTORY=/home/mehmet/vscode-workspaces/opcua-cps-kitchen
$PROJECT_DIRECTORY/build.bash
# _robot_position
# $PROJECT_DIRECTORY/build/start_robot_instance 1
ROBOTS=4

for ((robot_count = 0; robot_count < ROBOTS; robot_count++)); do
    robot_position=$(( $robot_count + 1 ))
    echo "Starting robot at position $robot_position"
    # $PROJECT_DIRECTORY/build/start_robot_instance $robot_position &
    $PROJECT_DIRECTORY/build/start_robot_instance $robot_position 1>/dev/null &
    # $PROJECT_DIRECTORY/build/start_robot_instance $robot_position >./logs/robot_${robot_position}_${ROBOTS}_$(date +%Y%m%d%H%M%S) &
    exit_code=$?
    if [ $exit_code -ne 0 ]; then
        echo "Error: Non-zero exit code detected. Exiting."
        break
    fi
    # sleep 0.5
done
echo "All robots started"
wait
echo "All robots terminated"

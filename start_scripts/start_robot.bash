#!/usr/bin/bash
PROJECT_DIRECTORY=/home/mehmet/vscode-workspaces/opcua-cps-kitchen
$PROJECT_DIRECTORY/build.bash
# _robot_position, _robot_port, _controller_port, _conveyor_port
# $PROJECT_DIRECTORY/build/start_robot_instance 1 4000 5000 6000
ROBOTS=4
START_PORT=4000
CONTROLLER_PORT=5000
CONVEYOR_PORT=6000

for ((robot_count = 0; robot_count < ROBOTS; robot_count++)); do
    robot_position=$(( $robot_count + 1 ))
    robot_port=$(( $START_PORT + $robot_count))
    echo "Starting robot with port $robot_port at position $robot_position"
    # $PROJECT_DIRECTORY/build/start_robot_instance $robot_position $robot_port $CONTROLLER_PORT $CONVEYOR_PORT &
    $PROJECT_DIRECTORY/build/start_robot_instance $robot_position $robot_port $CONTROLLER_PORT $CONVEYOR_PORT 1>/dev/null &
    # $PROJECT_DIRECTORY/build/start_robot_instance $robot_position $robot_port $CONTROLLER_PORT $CONVEYOR_PORT >./logs/robot_${robot_position}_${robot_port}_${ROBOTS}_$(date +%Y%m%d%H%M%S) &
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

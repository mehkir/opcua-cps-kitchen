#!/usr/bin/bash
PROJECT_DIRECTORY=/home/mehmet/vscode-workspaces/opcua-cps-kitchen
$PROJECT_DIRECTORY/build.bash
# _controller_port, _robot_start_port, _robot_count, _remote_conveyor_port, _conveyor_plates_count, _clock_port
$PROJECT_DIRECTORY/build/start_controller_instance 6000 4000 1 3000 2 5000
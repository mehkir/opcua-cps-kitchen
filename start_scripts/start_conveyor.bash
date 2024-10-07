#!/usr/bin/bash
PROJECT_DIRECTORY=/home/mehmet/vscode-workspaces/opcua-cps-kitchen
$PROJECT_DIRECTORY/build.bash
# _conveyor_port, _robot_start_port, _robot_count, _clock_port, _controller_port
$PROJECT_DIRECTORY/build/start_conveyor_instance  3000 4000 1 5000 6000
#!/usr/bin/bash
PROJECT_DIRECTORY=/home/mehmet/vscode-workspaces/opcua-cps-kitchen
$PROJECT_DIRECTORY/build.bash
# _robot_id, _robot_port, _clock_port, _controller_port, _conveyor_port
$PROJECT_DIRECTORY/build/start_robot_instance 0 4000 5000 6000

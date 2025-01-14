#!/usr/bin/bash
PROJECT_DIRECTORY=/home/mehmet/vscode-workspaces/opcua-cps-kitchen
$PROJECT_DIRECTORY/build.bash
# _conveyor_port, _robot_count
$PROJECT_DIRECTORY/build/start_conveyor_instance 6000 4
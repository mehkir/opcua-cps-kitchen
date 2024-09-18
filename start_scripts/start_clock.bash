#!/usr/bin/bash
PROJECT_DIRECTORY=/home/mehmet/vscode-workspaces/opcua-cps-kitchen
$PROJECT_DIRECTORY/build.bash
# _clock_port, _clock_client_count
$PROJECT_DIRECTORY/build/start_clock_instance 5000 2

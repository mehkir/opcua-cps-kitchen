#!/usr/bin/bash
PROJECT_DIRECTORY=/home/mehmet/vscode-workspaces/opcua-cps-kitchen
$PROJECT_DIRECTORY/build/start_controller_instance &
exit_code=$?
if [ $exit_code -ne 0 ]; then
    echo "Error: Non-zero exit code detected during controller startup. Exiting."
    exit $exit_code
fi
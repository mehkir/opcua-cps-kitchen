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
$PROJECT_DIRECTORY/build/start_kitchen_instance $ROBOTS &
exit_code=$?
if [ $exit_code -ne 0 ]; then
    echo "Error: Non-zero exit code detected during kitchen startup. Exiting."
    exit $exit_code
fi      
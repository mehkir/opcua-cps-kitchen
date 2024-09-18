#!/usr/bin/bash

cmake -B /home/mehmet/vscode-workspaces/opcua-cps-kitchen/build -S /home/mehmet/vscode-workspaces/opcua-cps-kitchen
#$(which cmake) --build /home/mehmet/vscode-workspaces/opcua-cps-kitchen/build/demos --config Release --target robot_demo -- -j$(nproc)
#$(which cmake) --build /home/mehmet/vscode-workspaces/opcua-cps-kitchen/build/demos --config Release --target clock_demo -- -j$(nproc)
$(which cmake) --build /home/mehmet/vscode-workspaces/opcua-cps-kitchen/build --config Release --target start_robot_instance -- -j$(nproc)
$(which cmake) --build /home/mehmet/vscode-workspaces/opcua-cps-kitchen/build --config Release --target start_clock_instance -- -j$(nproc)
$(which cmake) --build /home/mehmet/vscode-workspaces/opcua-cps-kitchen/build --config Release --target start_controller_instance -- -j$(nproc)
$(which cmake) --build /home/mehmet/vscode-workspaces/opcua-cps-kitchen/build --config Release --target start_conveyor_instance -- -j$(nproc)

#!/usr/bin/bash

cmake -B /home/mehmet/vscode-workspaces/opcua-cps-kitchen/build -S /home/mehmet/vscode-workspaces/opcua-cps-kitchen
$(which cmake) --build /home/mehmet/vscode-workspaces/opcua-cps-kitchen/build/demos --config Release --target robot_demo -- -j$(nproc)
$(which cmake) --build /home/mehmet/vscode-workspaces/opcua-cps-kitchen/build/demos --config Release --target clock_demo -- -j$(nproc)
$(which cmake) --build /home/mehmet/vscode-workspaces/opcua-cps-kitchen/build/demos --config Release --target string_server_for_subscription -- -j$(nproc)
$(which cmake) --build /home/mehmet/vscode-workspaces/opcua-cps-kitchen/build/demos --config Release --target string_client_for_subscription -- -j$(nproc)
$(which cmake) --build /home/mehmet/vscode-workspaces/opcua-cps-kitchen/build/tests --config Release --target information_node_tester -- -j$(nproc)
$(which cmake) --build /home/mehmet/vscode-workspaces/opcua-cps-kitchen/build --config Release --target start_robot_instance -- -j$(nproc)
$(which cmake) --build /home/mehmet/vscode-workspaces/opcua-cps-kitchen/build --config Release --target start_controller_instance -- -j$(nproc)
$(which cmake) --build /home/mehmet/vscode-workspaces/opcua-cps-kitchen/build --config Release --target start_conveyor_instance -- -j$(nproc)
$(which cmake) --build /home/mehmet/vscode-workspaces/opcua-cps-kitchen/build --config Release --target recipe_testframe -- -j$(nproc)
$(which cmake) --build /home/mehmet/vscode-workspaces/opcua-cps-kitchen/build --config Release --target session_id_testframe -- -j$(nproc)
$(which cmake) --build /home/mehmet/vscode-workspaces/opcua-cps-kitchen/build --config Release --target statistics-writer-main -- -j$(nproc)

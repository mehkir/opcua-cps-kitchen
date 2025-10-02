#!/usr/bin/bash

cmake -B /home/mehmet/vscode-workspaces/opcua-cps-kitchen/build_debug -S /home/mehmet/vscode-workspaces/opcua-cps-kitchen -DUSE_CUSTOM_VERSION=ON -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_FLAGS_DEBUG="-g -O0" -DCMAKE_CXX_FLAGS_DEBUG="-g -O0"
$(which cmake) --build /home/mehmet/vscode-workspaces/opcua-cps-kitchen/build_debug/demos --config Debug --target discovery_server -- -j$(nproc)

$(which cmake) --build /home/mehmet/vscode-workspaces/opcua-cps-kitchen/build_debug --config Debug --target \
    start_robot_instance \
    start_controller_instance \
    start_conveyor_instance \
    start_kitchen_instance \
    statistics-writer-main \
    -- -j$(nproc)

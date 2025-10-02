#!/usr/bin/bash

cmake -B /home/mehmet/vscode-workspaces/opcua-cps-kitchen/build -S /home/mehmet/vscode-workspaces/opcua-cps-kitchen -DUSE_CUSTOM_VERSION=ON
$(which cmake) --build /home/mehmet/vscode-workspaces/opcua-cps-kitchen/build/demos --config Release --target \
    robot_demo \
    clock_demo \
    string_server_for_subscription \
    string_client_for_subscription \
    object_node \
    browse_node \
    tutorial_server_object \
    object_type_node \
    discovery_server \
    client_discovery_lookup \
    client_discovery_lookup_minimal \
    server_method \
    client_call_method \
    -- -j$(nproc)

$(which cmake) --build /home/mehmet/vscode-workspaces/opcua-cps-kitchen/build/tests --config Release --target \
    information_node_tester \
    test_now_monotonic \
    -- -j$(nproc)

$(which cmake) --build /home/mehmet/vscode-workspaces/opcua-cps-kitchen/build --config Release --target \
    start_robot_instance \
    start_controller_instance \
    start_conveyor_instance \
    start_kitchen_instance \
    recipe_testframe \
    session_id_testframe \
    statistics-writer-main \
    -- -j$(nproc)

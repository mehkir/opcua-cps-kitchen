#!/usr/bin/bash
SCRIPT_PATH="$(realpath "$0")"
PROJECT_DIR="$(dirname "$SCRIPT_PATH")"

cmake -B "${PROJECT_DIR}/build" -S "${PROJECT_DIR}" -DUSE_CUSTOM_VERSION=ON
$(which cmake) --build "${PROJECT_DIR}/build/demos" --config Release --target \
    discovery_server \
    -- -j$(nproc)

$(which cmake) --build "${PROJECT_DIR}/build" --config Release --target \
    start_robot_instance \
    start_controller_instance \
    start_conveyor_instance \
    start_kitchen_instance \
    -- -j$(nproc)

#!/usr/bin/bash
SCRIPT_PATH="$(realpath "$0")"
PROJECT_DIR="$(dirname "$SCRIPT_PATH")"

cmake -B "${PROJECT_DIR}/build_sanitized" -S "${PROJECT_DIR}" -DUSE_CUSTOM_VERSION=ON -DUSE_TSAN=ON -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_FLAGS_DEBUG="-g -O0" -DCMAKE_CXX_FLAGS_DEBUG="-g -O0"
$(which cmake) --build "${PROJECT_DIR}/build_sanitized/demos" --config Debug --target discovery_server -- -j$(nproc)

$(which cmake) --build "${PROJECT_DIR}/build_sanitized" --config Debug --target \
    start_robot_instance \
    start_controller_instance \
    start_conveyor_instance \
    start_kitchen_instance \
    -- -j$(nproc)

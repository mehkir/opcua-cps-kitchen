#!/usr/bin/bash
SCRIPT_PATH="$(realpath "$0")"
PROJECT_DIR="$(dirname "$SCRIPT_PATH")"

cmake -B "${PROJECT_DIR}/build" -S "${PROJECT_DIR}"
$(which cmake) --build "${PROJECT_DIR}/build" --config Release --target doc -- -j$(nproc)
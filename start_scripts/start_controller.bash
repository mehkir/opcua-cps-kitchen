#!/usr/bin/bash
SCRIPT_PATH="$(realpath "$0")"
SCRIPT_DIR="$(dirname "$SCRIPT_PATH")"
PROJECT_DIRECTORY="$(cd -- "$SCRIPT_DIR/.." && pwd)"
exec "$PROJECT_DIRECTORY/build/start_controller_instance"
# exec "$PROJECT_DIRECTORY/build/start_controller_instance" >./logs/controller_$(date +%Y%m%d%H%M%S)
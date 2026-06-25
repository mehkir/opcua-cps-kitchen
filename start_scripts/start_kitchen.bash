#!/usr/bin/bash
if (( $# < 1 )); then
  echo "Usage: $0 <robots_count>"
  exit 1
fi
if ! [[ "$1" =~ ^[0-9]+$ ]] || (( $1 < 1 )); then
        echo "robots count must be >= 1"
        exit 1
fi
ROBOTS=$1

SCRIPT_PATH="$(realpath "$0")"
SCRIPT_DIR="$(dirname "$SCRIPT_PATH")"
PROJECT_DIRECTORY="$(cd -- "$SCRIPT_DIR/.." && pwd)"
exec "$PROJECT_DIRECTORY/build/start_kitchen_instance" "$ROBOTS"
# exec "$PROJECT_DIRECTORY/build/start_kitchen_instance" "$ROBOTS" >./logs/kitchen_${ROBOTS}_$(date +%Y%m%d%H%M%S)
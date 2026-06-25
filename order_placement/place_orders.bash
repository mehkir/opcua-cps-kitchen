#!/usr/bin/bash
# Validate argument
if (( $# < 1 )); then
  echo "Usage: $0 <order_count>"
  exit 1
fi

if ! [[ "$1" =~ ^[0-9]+$ ]] || (( $1 < 1 )); then
        echo "order count must be >= 1"
        exit 1
fi

SCRIPT_PATH="$(realpath "$0")"
SCRIPT_DIR="$(dirname "$SCRIPT_PATH")"
cd -- "$SCRIPT_DIR"
cd ..
PROJECT_DIRECTORY="$(pwd)"
ORDER_COUNT=$1

cd "$PROJECT_DIRECTORY/order_placement"
LIB_DIR="${PROJECT_DIRECTORY}/cps-kitchen-dashboard/my-addons/open62541/lib"
export LD_LIBRARY_PATH="$LIB_DIR${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
npm start -- --order-count $ORDER_COUNT

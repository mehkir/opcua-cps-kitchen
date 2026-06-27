#!/usr/bin/env bash
set -Eeuo pipefail

orders_count=""

usage() {
    echo "Usage: $0 -o <orders_count>"
}

while getopts ":o:" opt; do
    case "$opt" in
        o) orders_count="$OPTARG" ;;
        *) usage; exit 1 ;;
    esac
done
shift $((OPTIND - 1))


# Validate orders_count was provided
if [[ -z "$orders_count" ]]; then
  usage
  exit 1
fi

if ! [[ "$orders_count" =~ ^[0-9]+$ ]] || (( $orders_count < 1 )); then
        echo "order count must be >= 1"
        exit 1
fi

SCRIPT_PATH="$(realpath "$0")"
SCRIPT_DIR="$(dirname "$SCRIPT_PATH")"
cd -- "$SCRIPT_DIR"
cd ..
PROJECT_DIRECTORY="$(pwd)"
ORDER_COUNT=$orders_count

cd "$PROJECT_DIRECTORY/order_placement"
LIB_DIR="${PROJECT_DIRECTORY}/cps-kitchen-dashboard/my-addons/open62541/lib"
export LD_LIBRARY_PATH="$LIB_DIR${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
npm start -- --order-count $ORDER_COUNT

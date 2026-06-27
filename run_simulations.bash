#!/usr/bin/env bash
set -Eeuo pipefail

robots_count=""
orders_count=""
runs_count=""
startup_pid=""
dashboard_pid=""

usage() {
    echo "Usage: $0 -r <robots_count> -o <orders_count> -n <runs_count>"
}

while getopts ":r:o:n:" opt; do
    case "$opt" in
        r) robots_count="$OPTARG" ;;
        o) orders_count="$OPTARG" ;;
        n) runs_count="$OPTARG" ;;
        *) usage; exit 1 ;;
    esac
done

if [[ -z "$robots_count" || -z "$orders_count" || -z "$runs_count" ]]; then
    usage
    exit 1
fi

if (( robots_count < 1 )); then
    echo "robots_count must be >= 1"
    exit 1
fi

if (( orders_count < 1 )); then
    echo "orders_count must be >= 1"
    exit 1
fi

if (( runs_count < 1 )); then
    echo "runs_count must be >= 1"
    exit 1
fi

SCRIPT_PATH="$(realpath "$0")"
SCRIPT_DIR="$(dirname "$SCRIPT_PATH")"
PROJECT_DIRECTORY="$(cd -- "$SCRIPT_DIR/.." && pwd)"

wait_for_port() {
    local port="$1"
    local timeout="${2:-30}"
    local deadline=$((SECONDS + timeout))

    while (( SECONDS < deadline )); do
        if bash -c ":</dev/tcp/127.0.0.1/$port" 2>/dev/null; then
            return 0
        fi
        sleep 0.5
    done

    echo "Timeout waiting for port $port"
    return 1
}

stop_run() {
    trap - INT TERM EXIT

    echo "Stopping current simulation run..."

    if [[ -n "${startup_pid:-}" ]] && kill -0 "$startup_pid" 2>/dev/null; then
        kill -TERM "$startup_pid" 2>/dev/null || true
    fi

    if [[ -n "${dashboard_pid:-}" ]] && kill -0 "$dashboard_pid" 2>/dev/null; then
        kill -TERM "$dashboard_pid" 2>/dev/null || true
    fi

    if [[ -n "${startup_pid:-}" ]]; then
        wait "$startup_pid" 2>/dev/null || true
    fi

    if [[ -n "${dashboard_pid:-}" ]]; then
        wait "$dashboard_pid" 2>/dev/null || true
    fi

    startup_pid=""
    dashboard_pid=""
}

trap stop_run INT TERM EXIT

"$PROJECT_DIRECTORY/build.bash"

for run in $(seq 1 "$runs_count"); do
    echo "============================================================"
    echo "Starting simulation run $run/$runs_count"
    echo "robots=$robots_count orders=$orders_count"
    echo "============================================================"

    export CPS_KITCHEN_RUN_ID="robots_${robots_count}_orders_${orders_count}_run_${run}"

    "$PROJECT_DIRECTORY/start_scripts/startup_kitchen.bash" -r "$robots_count" -e -n &
    startup_pid="$!"

    "$PROJECT_DIRECTORY/start_scripts/start_dashboard.bash" -r "$robots_count" &
    dashboard_pid="$!"

    wait_for_port 8000 30
    wait_for_port 8080 30

    echo "System appears ready. Placing orders..."

    "$PROJECT_DIRECTORY/order_placement/place_orders.bash" -o "$orders_count"

    echo "Orders placed. Stopping run $run..."

    stop_run

    echo "Run $run finished."
    sleep 2
done

trap - INT TERM EXIT
echo "All simulation runs finished."
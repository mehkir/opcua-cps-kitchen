if (( $# < 1 )); then
  echo "Usage: $0 <robots_count>"
  exit 1
fi
if (( $1 < 1)); then
    echo "robots count must be >= 1"
    exit 1
fi
ROBOTS=$1

SCRIPT_PATH="$(realpath "$0")"
SCRIPT_DIR="$(dirname "$SCRIPT_PATH")"
cd -- "$SCRIPT_DIR"
cd ..
PROJECT_DIRECTORY="$(pwd)"
$PROJECT_DIRECTORY/build/statistics-writer-main $ROBOTS &
exit_code=$?
if [ $exit_code -ne 0 ]; then
    echo "Error: Non-zero exit code detected during statistics-writer-main startup. Exiting."
    exit $exit_code
fi

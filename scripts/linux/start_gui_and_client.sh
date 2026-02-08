#!/bin/bash
# Start GUI and CLI client together (Linux/macOS)
# Usage:
#   ./start_gui_and_client.sh                 # GUI + CLI (interactive)
#   ./start_gui_and_client.sh --gui-only      # GUI only
#   ./start_gui_and_client.sh --cli-only      # CLI only
#   ./start_gui_and_client.sh -c "GET_STATUS" # GUI + single CLI command

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RUN_CLIENT="$SCRIPT_DIR/run_client.sh"

if [ ! -f "$RUN_CLIENT" ]; then
    echo "ERROR: run_client.sh not found in $SCRIPT_DIR"
    exit 1
fi

GUI_ONLY=false
CLI_ONLY=false
CLI_ARGS=()

for arg in "$@"; do
    case "$arg" in
        --gui-only) GUI_ONLY=true ;;
        --cli-only) CLI_ONLY=true ;;
        *) CLI_ARGS+=("$arg") ;;
    esac
done

if [ "$CLI_ONLY" = false ]; then
    echo "Starting GUI..."
    "$RUN_CLIENT" --gui &
fi

if [ "$GUI_ONLY" = false ]; then
    echo "Starting CLI..."
    "$RUN_CLIENT" "${CLI_ARGS[@]}"
fi

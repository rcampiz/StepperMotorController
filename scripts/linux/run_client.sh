#!/bin/bash
# STM32 Motor Controller Client Launcher (Linux/macOS)
# Usage: ./run_client.sh [arguments]
# Example: ./run_client.sh --gui              Launch GUI
# Example: ./run_client.sh -p /dev/ttyACM0    Connect to port (CLI)
# Example: ./run_client.sh --list             List serial ports
# Example: ./run_client.sh --json -c "GET_STATUS"

set -e

# Get script and project directories
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$(dirname "$SCRIPT_DIR")")"
CLIENT_DIR="$PROJECT_ROOT/client"
VENV_DIR="$CLIENT_DIR/.venv"
REQUIREMENTS_FILE="$CLIENT_DIR/requirements.txt"

echo "========================================"
echo "STM32 Motor Controller Client"
echo "========================================"
echo ""

# Check Python version
echo "Checking Python version..."
if command -v python3 &> /dev/null; then
    PYTHON=python3
elif command -v python &> /dev/null; then
    PYTHON=python
else
    echo "  ERROR: Python not found. Please install Python 3.10+"
    exit 1
fi

PYTHON_VERSION=$($PYTHON --version 2>&1)
echo "  Found: $PYTHON_VERSION"

# Extract version and check
MAJOR=$(echo "$PYTHON_VERSION" | sed -E 's/Python ([0-9]+)\..*/\1/')
MINOR=$(echo "$PYTHON_VERSION" | sed -E 's/Python [0-9]+\.([0-9]+).*/\1/')

if [ "$MAJOR" -lt 3 ] || ([ "$MAJOR" -eq 3 ] && [ "$MINOR" -lt 10 ]); then
    echo "  ERROR: Python 3.10+ required"
    exit 1
fi

# Create virtual environment if needed
if [ ! -d "$VENV_DIR" ]; then
    echo ""
    echo "Creating virtual environment..."
    $PYTHON -m venv "$VENV_DIR"
    echo "  Created: $VENV_DIR"
fi

# Activate virtual environment
echo ""
echo "Activating virtual environment..."
source "$VENV_DIR/bin/activate"

# Install/update dependencies
echo ""
echo "Checking dependencies..."
pip install -r "$REQUIREMENTS_FILE" --quiet
echo "  Dependencies OK"

# Check for install-only mode
if [ "$1" = "--install-only" ]; then
    echo ""
    echo "Installation complete."
    exit 0
fi

# Run the client
echo ""
echo "Starting client..."
echo "----------------------------------------"
python "$CLIENT_DIR/src/main.py" "$@"
EXIT_CODE=$?

echo "----------------------------------------"
echo "Client exited with code: $EXIT_CODE"
exit $EXIT_CODE

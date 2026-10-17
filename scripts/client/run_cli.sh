#!/usr/bin/env bash
# Start the motor-controller command line (smc) (client/smc_cli) on Linux / Raspberry Pi. First run creates
# client/.venv and installs the requirements. Arguments pass through, e.g.:  ./run_cli.sh info --sim
# Serial access on the Pi: add yourself to the dialout group (sudo usermod -aG dialout $USER).
set -e
CLIENT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../client" && pwd)"
PY="$CLIENT/.venv/bin/python"
if [ ! -x "$PY" ]; then
    echo "Creating client/.venv and installing requirements ..."
    python3 -m venv "$CLIENT/.venv"
    "$PY" -m pip install -q -r "$CLIENT/requirements.txt"
fi
cd "$CLIENT"
exec "$PY" -m smc_cli "$@"

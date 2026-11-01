#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
duration=${1:-86400}
python=${TEST_PYTHON:-.venv312/bin/python}
mkdir -p logs
"$python" tests/soak/load.py "$((duration + 60))" > logs/soak-load.log 2>&1 &
load_pid=$!
trap 'kill -TERM "$load_pid" 2>/dev/null || true; wait "$load_pid" 2>/dev/null || true' EXIT INT TERM
attempt=0
while ! grep -q LOAD_READY logs/soak-load.log; do
 kill -0 "$load_pid"
 attempt=$((attempt+1))
 [ "$attempt" -lt 30 ] || exit 1
 sleep 1
done
"$python" tests/soak/monitor.py --seconds "$duration" --output logs/soak-resources.csv

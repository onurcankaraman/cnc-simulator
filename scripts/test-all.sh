#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
python3 scripts/generate-contract.py
docker run --rm -v "$PWD:/app" -w /app cnc-dependencies sh -c 'cmake -S . -B build -G Ninja && cmake --build build -j2 && ctest --test-dir build --output-on-failure && build/stream-memory && cp /tmp/cnc-traces.csv logs/synthetic-traces.csv'
scripts/smoke-connectors.sh

#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
python3 scripts/generate-contract.py
scripts/test-database.sh
scripts/smoke-connectors.sh
${TEST_PYTHON:-.venv312/bin/python} tests/integration/program.py
${TEST_PYTHON:-.venv312/bin/python} tests/integration/timescale.py
${TEST_PYTHON:-.venv312/bin/python} tests/integration/security.py
${TEST_PYTHON:-.venv312/bin/python} tests/e2e/recovery.py
${TEST_PYTHON:-.venv312/bin/python} tests/e2e/database_write_failure.py
${TEST_PYTHON:-.venv312/bin/python} tests/e2e/admin.py

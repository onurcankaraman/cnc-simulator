#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
${TEST_PYTHON:-.venv312/bin/python} tests/integration/connectors.py

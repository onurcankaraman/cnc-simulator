#!/bin/sh
set -eu
curl -fsS --max-time 3 http://127.0.0.1:8080/health/ready >/dev/null
if [ "${ENABLE_SQL_PROXY:-false}" = true ]; then
  pg_isready -q -h 127.0.0.1 -p 6432 -U cnc_demo -d cnc -t 2
fi

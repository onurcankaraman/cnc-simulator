#!/bin/bash
set -euo pipefail
if [[ ${ENABLE_SQL_PROXY:-false} != true ]]; then
  exec /app/cnc-simulator "$@"
fi
config=${SQL_PROXY_CONFIG:-/app/config/pgbouncer.ini}
test -r "$config"
test -r /run/secrets/sql-proxy-users
pids=()
cleanup() {
  trap '' TERM INT
  kill -TERM "${pids[@]}" 2>/dev/null || true
  wait "${pids[@]}" 2>/dev/null || true
}
trap 'cleanup; exit 0' TERM INT
pgbouncer "$config" &
pids+=("$!")
/app/cnc-simulator "$@" &
pids+=("$!")
# Failure of either process restarts the whole container via Docker policy.
set +e
wait -n "${pids[@]}"
set -e
cleanup
exit 1

#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
# Dedicated disposable database; never truncate application telemetry or runtime state.
docker compose exec -T timescaledb dropdb -U postgres --if-exists cnc_test
docker compose exec -T timescaledb createdb -U postgres cnc_test
docker compose exec -T timescaledb psql -v ON_ERROR_STOP=1 -U postgres -d cnc_test < timescaledb/init/01-schema.sql > logs/timescale-test-schema.log
docker compose exec -T timescaledb psql -U postgres -d cnc_test -c "SELECT delete_job(job_id) FROM timescaledb_information.jobs WHERE proc_name='cnc_retention_job'" > /dev/null
docker compose -f compose.yaml -f compose.test.yaml build tests > logs/timescale-test-build.log 2>&1
docker compose -f compose.yaml -f compose.test.yaml run --rm tests

#!/bin/sh
set -eu
writer=$(cat /run/secrets/db-writer-password)
reader=$(cat /run/secrets/db-reader-password)
# Generated hex passwords only; passed through psql variables, never interpolated as SQL.
psql -v ON_ERROR_STOP=1 --username "$POSTGRES_USER" --dbname "$POSTGRES_DB" -v writer="$writer" -v reader="$reader" <<'SQL'
CREATE ROLE cnc_writer LOGIN PASSWORD :'writer';
CREATE ROLE cnc_reader LOGIN PASSWORD :'reader';
GRANT CONNECT ON DATABASE cnc TO cnc_writer,cnc_reader;
GRANT USAGE ON SCHEMA public TO cnc_writer,cnc_reader;
GRANT SELECT,INSERT,UPDATE ON telemetry,events,orders,runtime_state TO cnc_writer;
GRANT EXECUTE ON FUNCTION cnc_cleanup(bigint) TO cnc_writer;
GRANT SELECT ON telemetry,events,orders TO cnc_reader;
ALTER ROLE cnc_reader SET default_transaction_read_only=on;
ALTER ROLE cnc_reader SET statement_timeout='15s';
SQL

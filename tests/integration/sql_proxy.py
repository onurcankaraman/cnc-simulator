#!/usr/bin/env python3
"""Independent SQL proxy checks. Credentials are read from a private file."""
import json, os, pathlib
import psycopg
password = pathlib.Path(os.environ['SQL_PROXY_PASSWORD_FILE']).read_text().strip()
params = dict(host=os.getenv('SQL_PROXY_HOST', '127.0.0.1'),
              port=int(os.getenv('SQL_PROXY_PORT', '6432')), dbname='cnc',
              user='cnc_demo', password=password, sslmode='disable',
              connect_timeout=10, autocommit=True)
with psycopg.connect(**params) as db:
    assert not db.pgconn.ssl_in_use
    assert db.execute('SELECT ssl FROM pg_stat_ssl WHERE pid=pg_backend_pid()').fetchone()[0]
    row = db.execute('SELECT seq, payload_json, nc_json FROM telemetry ORDER BY ts_ms DESC LIMIT 1').fetchone()
    assert len(row[1]['data']) == 158 and row[2]['revision'] == 2
    assert row[1]['sequence'] == row[0]
    for table in ('events', 'orders'):
        db.execute('SELECT 1 FROM ' + table + ' LIMIT 1').fetchone()
    db.execute('SET default_transaction_read_only=off')
    for query in ('SELECT * FROM runtime_state LIMIT 1',
                  'UPDATE telemetry SET seq=seq WHERE false',
                  'CREATE TABLE public.proxy_permission_probe(id int)'):
        db.execute('BEGIN')
        try:
            db.execute(query)
        except psycopg.errors.InsufficientPrivilege:
            pass
        else:
            raise AssertionError('forbidden query accepted: ' + query)
        finally:
            db.execute('ROLLBACK')
for overrides in ({'password': 'invalid-password'}, {'user': 'cnc_writer'}, {'dbname': 'postgres'}):
    try:
        with psycopg.connect(**(params | overrides)):
            raise AssertionError('unauthorized connection accepted')
    except psycopg.OperationalError:
        pass
print(json.dumps({'result': 'PASS', 'client_tls': False, 'backend_tls': True,
                  'sequence': row[0], 'fields': 158, 'nc_revision': 2,
                  'read_only': True, 'private_state': 'DENIED',
                  'bad_password_writer_other_database': 'DENIED'}))

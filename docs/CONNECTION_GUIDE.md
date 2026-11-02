# Connections and operations

All addresses below are localhost defaults. Substitute only an authorized private LAN/VPN address with firewall restrictions. The application is synthetic; the S7 interface is a virtual DB service, not real Siemens firmware.

## S7

Host `127.0.0.1`, port `102` (or `.env` S7_PORT), rack `0`, slot `1`. Slot `0` also passed the independent client test. Container port is 1102. DB sizes are 256,256,256,128 bytes for DB1..DB4. Maximum clients: 8. Classic S7 DB reads are supported; write callbacks reject external writes at the protocol level.

```python
import snap7, struct
c = snap7.client.Client()
c.connect('127.0.0.1', 0, 1, tcp_port=102)
rpm = struct.unpack('>f', c.db_read(1, 20, 4))[0]
wear = struct.unpack('>f', c.db_read(2, 4, 4))[0]
sequence = struct.unpack('>I', c.db_read(1, 8, 4))[0]
c.disconnect()
```

Read sequence before and after a multi-DB read; retry if it changed. U8 flags are whole bytes (0/1), not packed Siemens BOOL fields. Use byte reads; bit-word-length reads are deliberately rejected by this adapter. See `contracts/tags.csv` and `contracts/s7_map.md` for all offsets.

## OPC UA

Endpoint `opc.tcp://127.0.0.1:4840`. Select **Basic256Sha256**, **SignAndEncrypt**. Trust `secrets/server.der` in the client. Use `secrets/client.der` + `secrets/client-key.pem` for the supplied test client; its application URI is `urn:cnc:test-client`. The server trusts that client certificate at startup. For another client, replace the configured `OPCUA_CLIENT_CERT` mount with its public certificate and restart. Do not copy client private keys to the server for operational clients.

Server ApplicationUri: `urn:digitheta:cnc-simulator:server`. Namespace URI: `urn:digitheta:cnc-simulator:machine:v1`, verified index 2. The object tree is `Objects/CNC-01/{Live,Production,Tools,Alarms,Metadata}` with `Tools/Tool01..Tool12`. All contract NodeIds remain `ns=2;s=Machine.CNC-01.<key>` regardless of browse grouping. Datatypes match CSV, SourceTimestamp is snapshot UTC, and subscriptions work. Maximum 10 sessions/subscriptions, 2,000 monitored items overall, 200 per subscription.

Examples: `ns=2;s=Machine.CNC-01.sequence`, `ns=2;s=Machine.CNC-01.spindle_actual_rpm`, `ns=2;s=Machine.CNC-01.tool_01_wear_pct`. Metadata node: `ns=2;s=Machine.CNC-01.Metadata.DataOrigin` = `Synthetic`.

None-policy discovery may be available, but an insecure session is disallowed by default. Only for isolated private tests set `ALLOW_INSECURE_OPCUA=true` in `.env`, then recreate the service. No companion-specification compliance is claimed.

## MQTT

Broker `127.0.0.1:1883`, MQTT 3.1.1 or 5. User `observer` has read-only access to `plant/demo/cnc/CNC-01/#`; password is in `secrets/observer-password`. The simulator publisher has a separate credential. Anonymous access is disabled. Unencrypted 1883 is for loopback/private VPN use only.

```sh
docker compose exec -T mqtt-broker sh -c \
 'mosquitto_sub -h localhost -u observer -P "$(cat /run/secrets/observer-password)" -t plant/demo/cnc/CNC-01/telemetry -C 1'
```

| Suffix | QoS | Retained | Cadence |
|---|---:|---|---|
| telemetry | 0 | no | 1 second or state/event publication |
| state | 1 | yes | change, reconnect, periodic heartbeat |
| tools | 1 | yes | events, reconnect, periodic update |
| events | 1 | no | immediate engine events |
| availability | 1 | yes | online heartbeat / offline LWT |

Every message carries `schema_version`, `machine_id`, `ts_utc`, `sequence`, `data`. Telemetry's data object has all 158 contract keys and validates against `contracts/mqtt.schema.json`. Tool messages use `data.tools` with 12 entries. LWT timestamp/sequence refers to connection setup, not the unknown instant of a future disconnect. Compare telemetry to S7/UA only at equal sequence.

## HTTP and admin

Read APIs: `/health/live`, `/health/ready`, `/api/v1/about`, `/api/v1/status`, `/api/v1/history`, `/api/v1/events`, `/api/v1/tools`, `/api/v1/orders`, `/api/v1/metrics`. Readiness is degraded when persistence or MQTT is unavailable; liveness continues during recoverable disk/broker faults.

History example: `/api/v1/history?limit=1440&step=60`. Optional `from` and `to` are ISO8601 UTC `YYYY-MM-DDTHH:MM:SS[.sss]Z`. Numeric step is seconds, 1..86400; limit 1..5000. Results are chronological, filtered to the last 24h, selecting the first actual sample per bucket. For pagination, advance `from` by 1 ms after the last timestamp. Responses stream as chunked JSON; limit page sizes on slow connections. Events are newest-first and support `limit`.

Admin is **disabled by default** (404). To test it, set `ENABLE_ADMIN=true`; the generated `secrets/admin-token` is required as `Authorization: Bearer <token>`. It uses constant-time comparison, one accepted request per second and SQL audit events. No browser UI exposes controls. Endpoints accept JSON objects:

| POST path | Body |
|---|---|
| `/admin/scenario` | `{"profile":"aging_tools"}` |
| `/admin/inject-fault` | `{"code":1402}` |
| `/admin/ack-alarm` | `{}` |
| `/admin/replace-tool` | `{"slot":1}`; machine must be stopped |
| `/admin/set-seed` | `{"seed":42}` |
| `/admin/trigger-workorder` | `{}`; machine must be idle/stopped and fault-free |

Acknowledgement does not bypass physical recovery. Internal synthetic operator recovery is timed; feed hold resumes, severe faults discard unfinished work. Do not expose admin on a public interface. Rotate credentials by replacing ignored secret files, rerunning bootstrap to regenerate broker password hashes, and recreating the services.


## Direct PostgreSQL / TimescaleDB (local)

DBeaver, DataGrip, pgAdmin or psql: host `127.0.0.1`, port `15432`, database `cnc`, username `cnc_reader`. Read its generated password from `secrets/db-reader-password`. Local Docker transport uses PostgreSQL SCRAM password authentication, without TLS; it is bound only to loopback. For a later remote deployment use an SSH tunnel or configure verified PostgreSQL TLS; do not expose this port publicly. `POSTGRES_PORT` changes the host port only.

```sh
cat secrets/db-reader-password
psql -h 127.0.0.1 -p 15432 -U cnc_reader -d cnc -W
```

```sql
SELECT to_timestamp(ts_ms / 1000.0) AS time, spindle_rpm, load_pct, wear_pct
FROM telemetry
WHERE ts_ms >= (extract(epoch FROM now()) * 1000)::bigint - 86400000
ORDER BY ts_ms DESC LIMIT 100;
SELECT payload_json->'data' FROM telemetry ORDER BY ts_ms DESC LIMIT 1;
SELECT * FROM events ORDER BY ts_ms DESC LIMIT 100;
SELECT * FROM orders ORDER BY order_id DESC;
```

`cnc_reader` can SELECT telemetry/events/orders, cannot write or read private runtime checkpoints. The app uses `cnc_writer`; its password is `secrets/db-writer-password`. Administrative database user `postgres` uses `secrets/db-admin-password`, not the server SSH password. Do not share the admin or writer credentials for normal developer access. Secrets are generated only on bootstrap; changing the files after database initialization does not rotate PostgreSQL passwords.

Telemetry/events retain 24 hours with up to 60 seconds cleanup delay. Runtime state and active orders survive cleanup and container restart. Data is held in the `timescale-data` volume. Backup using PostgreSQL tools:

```sh
docker compose exec -T timescaledb pg_dump -U postgres -d cnc -Fc > cnc-backup.dump
```

Keep backup files private. SQLite files from the previous version are not read or automatically imported. Production was migrated with a validated checkpoint/history copy on 2026-11-02.


## Live TimescaleDB access (2026-11-02)

Web `https://cnc.digitheta.dev` and native MQTT TLS `cnc-mqtt.digitheta.dev:443` are unchanged, including existing viewer/observer credentials. S7/OPC UA still require an authorized operator tunnel; admin remains disabled.

Direct SQL uses a dedicated forwarding-only SSH account, not a public PostgreSQL port:

| Setting | Value |
|---|---|
| SSH host / port | `cnc.digitheta.dev` / `22` |
| SSH user | `cnc-db` |
| PostgreSQL host / port inside tunnel | `127.0.0.1` / `15432` |
| Database / user | `cnc` / `cnc_reader` |

Both passwords are distributed privately. DBeaver/DataGrip: enable SSH tunneling and use the literal database host `127.0.0.1`. Do not use `localhost` as the remote forwarding target: the account permits only `127.0.0.1:15432`. No shell, SFTP, TTY, remote forwarding or other target ports are allowed. SQL transport is encrypted by SSH; PostgreSQL SSL is not required on this private path.

```sh
ssh -N -L 25432:127.0.0.1:15432 cnc-db@cnc.digitheta.dev
# Separate terminal; prompts for the SQL reader password:
psql -h 127.0.0.1 -p 25432 -U cnc_reader -d cnc -W
```

Use the existing table queries above. `cnc_reader` can read telemetry/events/orders, but cannot modify data or read private runtime checkpoints. Operator runbook and secrets are under `/opt/cnc-simulator` on the host; never publish those files.


## Public demo PostgreSQL over TLS (2026-11-02)

The owner explicitly authorized worldwide access to the synthetic demo database. SSH is no longer required for the **public demo account**. Earlier SSH-only guidance above is superseded for this account; local Compose defaults still bind only to loopback.

| Setting | Value |
|---|---|
| Host | `cnc.digitheta.dev` |
| Port | `5432` |
| Database | `cnc` |
| User | `cnc_demo` |
| Password | Distributed privately; generated 32 random bytes / 64 hex characters |
| SSL mode | **`verify-full`** |
| Root CA | [ISRG Root X1 PEM](certificates/cnc-demo-root-ca.pem), verified against the served certificate chain |

DBeaver/DataGrip: select PostgreSQL, enter these settings, enable SSL, supply the downloaded PEM as the root/CA certificate and verify the hostname. No SSH settings or client certificate are needed. The hostname is also used by the existing HTTPS website; PostgreSQL uses its own port.

```sh
psql "host=cnc.digitheta.dev port=5432 dbname=cnc user=cnc_demo sslmode=verify-full sslrootcert=/path/to/cnc-demo-root-ca.pem gssencmode=disable connect_timeout=15" -W
```

The account can SELECT `telemetry`, `events` and `orders`; writes, schema changes and private `runtime_state` access are not granted. A connection limit of five and query/temporary-file limits bound ordinary demo use. TLS is mandatory; plaintext, wrong passwords, incorrect certificate hostnames and public administrator/writer/private-reader connections were tested and rejected. Existing REST/MQTT endpoints and credentials are unchanged. Old private SSH reader connections now also require PostgreSQL SSL (`require` for loopback tunnel use).

Certificates renew through existing Traefik ACME management. A dedicated host timer copies only this hostname's renewed certificate into the database's read-only TLS directory and reloads PostgreSQL every 12 hours when it changes. Admin/writer secrets and the TLS private key stay on the server and are not in this repository. The bundled CA is a public trust anchor, not a server certificate or private key.

See [PostgreSQL SSL and HBA rules](https://www.postgresql.org/docs/17/auth-pg-hba-conf.html) and [actual external test evidence](evidence/public-sql-tls.log). **24h soak = NOT RUN.**

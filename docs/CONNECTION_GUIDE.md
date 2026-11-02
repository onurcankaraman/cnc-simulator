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

Keep backup files private. SQLite files from the previous version are not read or automatically imported. Production is still on the previous deployment until an explicit, tested cutover.

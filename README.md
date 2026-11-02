# CNC Simulator

A C++17 synthetic CNC machine with real Snap7 Server, open62541 OPC UA, libmosquitto publishing, a bundled Mosquitto broker, TimescaleDB/PostgreSQL history, and a static dashboard. One `MachineSnapshot` supplies all 158 CSV contract fields. No Python/Node production runtime, external cloud, CDN, 3D, or real PLC control.

**TimescaleDB deployment:** one Compose project, three containers (C++ runtime, Mosquitto, TimescaleDB). Direct SQL access is available. Old SQLite measurements do not validate this revision; see [test report](docs/TEST_REPORT.md). **24h soak = NOT RUN**.

## Start

Requirements: Docker Engine/Desktop with Compose, OpenSSL, a POSIX shell, and network access for the first image build. Production is Linux arm64/x86_64; the verified environment is Linux arm64 in Docker Desktop.

```sh
./scripts/bootstrap.sh
docker compose up -d --build
docker compose ps
curl -fsS http://127.0.0.1:8080/health/ready
```

Open [the dashboard](http://127.0.0.1:8080). Bootstrap creates random local credentials and self-signed server/test-client certificates in ignored `secrets/`; it does not overwrite existing secrets. Never commit that directory. Data and checkpoints persist in the `timescale-data` Docker volume. Stop with `docker compose down`; **do not use `down -v`** if counters/tool life must survive. Existing `data/cnc.sqlite` is not modified, imported or used by the new runtime. New local databases start with fresh state. The live deployment was migrated with a validated checkpoint/history copy on 2026-11-02; see the test report.

Default host binding is **127.0.0.1**, admin is disabled, MQTT requires authentication, and OPC UA requires an encrypted channel. Ports are S7 **102**, OPC UA **4840**, MQTT **1883**, HTTP **8080**, PostgreSQL **15432** (always loopback). If 102 is occupied, set `S7_PORT=1102` in `.env` and use that port in clients. Internal S7 remains 1102; no bind-service capability or root runtime is needed.

For authorized private LAN/VPN access, set `BIND_IP` to that private interface and restrict source IPs with your host firewall. Reissue OPC certificates for the actual hostname/IP. Do not expose S7, HTTP/admin, MQTT, or OPC UA to the Internet. The Compose bridge is dedicated to the simulator; no real PLC addresses are configured or contacted.

## Connect

| Protocol | Address | Example |
|---|---|---|
| S7 | `127.0.0.1:102`, rack 0, slot 1 (slot 0 also tested) | `DB1.DBD20` spindle actual REAL32; `DB2.DBD4` active wear |
| OPC UA | `opc.tcp://127.0.0.1:4840` | `ns=2;s=Machine.CNC-01.spindle_actual_rpm` |
| MQTT | `127.0.0.1:1883`, user `observer` | `plant/demo/cnc/CNC-01/telemetry` |
| PostgreSQL | `127.0.0.1:15432`, database `cnc`, user `cnc_reader` | Password: `secrets/db-reader-password` |
| HTTP | `http://127.0.0.1:8080` | `/api/v1/status`, `/api/v1/history`, `/api/v1/tools` |

Use Basic256Sha256 / SignAndEncrypt and the generated client certificate for OPC UA. MQTT passwords are generated files, not literal values in this README. Full client examples, certificate trust, schemas and admin instructions are in [CONNECTION_GUIDE.md](docs/CONNECTION_GUIDE.md).

## Model and configuration

`SPEC.md` and `contracts/tags.csv` are normative. CMake uses generated contract definitions; `scripts/generate-contract.py` rejects overlaps, bounds errors and inconsistent NodeIds. All DB numeric fields are big endian; strings use S7 STRING headers. Four configured NC templates drive loading, spindle acceleration, movement, cutting, tool changes and inspection. Only inspection completes a part. Tool life, energy, counts, orders and random-generator state persist.

Configure program segments, 12 tools, fault hazards and synthetic profiles under `config/`, then restart the simulator. The native YAML parser validates the core machine/program/tool constraints. The production clock is monotonic and 1x. TimescaleDB keeps a rolling 24-hour UTC history plus persistent runtime state. Dashboard history starts empty and accumulates naturally. OEE is scoped to the current shift.

The scenario profiles are `normal`, `aging_tools`, `high_utilization`, `unstable_coolant`, `frequent_microstops`, `quality_issue`, `night_shift`, and `maintenance_week`. They affect wear stress, setup timing, fault hazards or quality; they do not arbitrarily scale all sensor outputs.

## Reproduce verification

Python is used only for independent testing and contract generation. Use **Python 3.12**; the pinned asyncua client is incompatible with Python 3.14.

```sh
python3.12 -m venv .venv312
.venv312/bin/pip install -r tests/requirements.txt
./scripts/test-all.sh
```

The recovery test intentionally restarts only this project's simulator, broker and database. Database tests use a disposable `cnc_test` database, never truncate live `cnc` tables. C++ tests run in a separate Docker build target against real TimescaleDB. Startup aborts if durable state cannot be read. Connection failure during operation is visible in health and a persisted `sample_gap` after recovery.

For the real 24-hour, 3-clients-per-protocol test, leave both commands running for the full duration:

```sh
./scripts/soak.sh 86400
```

A short test must not be reported as a completed soak. RSS is process RSS for simulator + broker, not cgroup cache. `scripts/test-all.sh` builds and runs the C++ tests against TimescaleDB; `docs/SBOM-*.tsv`, `LICENSES/`, and `THIRD_PARTY_NOTICES.md` record actual dependencies and license notices.

## Why three containers?

A single container is technically possible with a process supervisor, but would couple PostgreSQL, broker and simulator restarts, privileges and health checks. This deployment uses one command (`docker compose up -d --build`) with three independently managed containers. Database credentials are generated locally and excluded from Git; SQL is not Internet-exposed. The ≤100 MiB simulator+broker RSS target remains; TimescaleDB and full-stack RSS are measured separately, not hidden inside the original figure.

## Live public demo SQL

With the owner's authorization, the deployed demo database is also reachable directly at `cnc.digitheta.dev:5432`, database `cnc`, read-only user `cnc_demo`. Use **TLS `verify-full`** and the [public root CA](docs/certificates/cnc-demo-root-ca.pem); obtain the strong password privately. SSH is not required. Administrator/writer accounts remain blocked from the Internet. See the [connection guide](docs/CONNECTION_GUIDE.md#public-demo-postgresql-over-tls-2026-11-02). This does not change local Compose defaults or expose S7/admin.

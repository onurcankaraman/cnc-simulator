# CNC Simulator

A C++17 synthetic CNC machine with real Snap7 Server, open62541 OPC UA, libmosquitto publishing, a bundled Mosquitto broker, SQLite WAL history, and a static dashboard. One `MachineSnapshot` supplies all 158 CSV contract fields. No Python/Node production runtime, external cloud, CDN, 3D, or real PLC control.

**Working implementation with measured short-run tests:** 3 S7 + 3 encrypted OPC UA + 3 MQTT clients for 300.11 seconds; sampled peak simulator+broker RSS **23.906 MiB**, mean CPU **0.936% of one core**. Full 24-hour acceptance is pending; **24h soak = NOT RUN**. See [test evidence](docs/TEST_REPORT.md) and [limits](docs/LIMITATIONS.md).

## Start

Requirements: Docker Engine/Desktop with Compose, OpenSSL, a POSIX shell, and network access for the first image build. Production is Linux arm64/x86_64; the verified environment is Linux arm64 in Docker Desktop.

```sh
./scripts/bootstrap.sh
docker compose up -d --build
docker compose ps
curl -fsS http://127.0.0.1:8080/health/ready
```

Open [the dashboard](http://127.0.0.1:8080). Bootstrap creates random local credentials and self-signed server/test-client certificates in ignored `secrets/`; it does not overwrite existing secrets. Never commit that directory. Data persists in `data/cnc.sqlite`. Stop with `docker compose down`; do not delete `data/` if counters and tool life must survive.

Default host binding is **127.0.0.1**, admin is disabled, MQTT requires authentication, and OPC UA requires an encrypted channel. Ports are S7 **102**, OPC UA **4840**, MQTT **1883**, HTTP **8080**. If 102 is occupied, set `S7_PORT=1102` in `.env` and use that port in clients. Internal S7 remains 1102; no bind-service capability or root runtime is needed.

For authorized private LAN/VPN access, set `BIND_IP` to that private interface and restrict source IPs with your host firewall. Reissue OPC certificates for the actual hostname/IP. Do not expose S7, HTTP/admin, MQTT, or OPC UA to the Internet. The Compose bridge is dedicated to the simulator; no real PLC addresses are configured or contacted.

## Connect

| Protocol | Address | Example |
|---|---|---|
| S7 | `127.0.0.1:102`, rack 0, slot 1 (slot 0 also tested) | `DB1.DBD20` spindle actual REAL32; `DB2.DBD4` active wear |
| OPC UA | `opc.tcp://127.0.0.1:4840` | `ns=2;s=Machine.CNC-01.spindle_actual_rpm` |
| MQTT | `127.0.0.1:1883`, user `observer` | `plant/demo/cnc/CNC-01/telemetry` |
| HTTP | `http://127.0.0.1:8080` | `/api/v1/status`, `/api/v1/history`, `/api/v1/tools` |

Use Basic256Sha256 / SignAndEncrypt and the generated client certificate for OPC UA. MQTT passwords are generated files, not literal values in this README. Full client examples, certificate trust, schemas and admin instructions are in [CONNECTION_GUIDE.md](docs/CONNECTION_GUIDE.md).

## Model and configuration

`SPEC.md` and `contracts/tags.csv` are normative. CMake uses generated contract definitions; `scripts/generate-contract.py` rejects overlaps, bounds errors and inconsistent NodeIds. All DB numeric fields are big endian; strings use S7 STRING headers. Four configured NC templates drive loading, spindle acceleration, movement, cutting, tool changes and inspection. Only inspection completes a part. Tool life, energy, counts, orders and random-generator state persist.

Configure program segments, 12 tools, fault hazards and synthetic profiles under `config/`, then restart the simulator. The native YAML parser validates the core machine/program/tool constraints. The production clock is monotonic and 1x. SQLite keeps a rolling 24-hour UTC history plus persistent runtime state. Dashboard history starts empty and accumulates naturally. OEE is scoped to the current shift.

The scenario profiles are `normal`, `aging_tools`, `high_utilization`, `unstable_coolant`, `frequent_microstops`, `quality_issue`, `night_shift`, and `maintenance_week`. They affect wear stress, setup timing, fault hazards or quality; they do not arbitrarily scale all sensor outputs.

## Reproduce verification

Python is used only for independent testing and contract generation. Use **Python 3.12**; the pinned asyncua client is incompatible with Python 3.14.

```sh
python3.12 -m venv .venv312
.venv312/bin/pip install -r tests/requirements.txt
docker build --target dependencies -t cnc-dependencies .
./scripts/test-all.sh
.venv312/bin/python tests/e2e/recovery.py
.venv312/bin/python tests/e2e/admin.py
.venv312/bin/python tests/e2e/disk_fault.py
```

The recovery test intentionally restarts these two Compose services. The admin and ENOSPC tests use disposable isolated containers on ports 18080/18081 and never fill the host disk. They clean up their containers afterward.

For the real 24-hour, 3-clients-per-protocol test, leave both commands running for the full duration:

```sh
./scripts/soak.sh 86400
```

A short test must not be reported as a completed soak. RSS is process RSS for simulator + broker, not cgroup cache. Image builds include C++ tests; `docs/SBOM-*.tsv`, `LICENSES/`, and `THIRD_PARTY_NOTICES.md` record actual dependencies and license notices.

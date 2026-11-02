# Live TimescaleDB cutover — 2026-11-02 (Europe/Istanbul)

Application commit **0ff0a99** deployed on Linux x86_64 using Docker Compose project `cnc-live`. Only the CNC simulator container was replaced; the existing broker and all 47 unrelated containers retained their IDs. One new TimescaleDB container was added. All three CNC services are healthy. Traefik was not changed or restarted.

A consistent SQLite backup was first imported into a disposable rehearsal database and compared column-by-column, including JSON and runtime checkpoints. The final cutover stopped only the simulator, took another SQLite backup, and transactionally copied **7,036 telemetry rows, 3,338 events, 3 orders and 1 runtime checkpoint** into the empty production TimescaleDB database. Full per-row comparison passed before starting the new runtime. Cutover window: **07:57:31–07:57:50 UTC / 10:57:31–10:57:50 Istanbul (19 seconds)**. Immediately after startup: **219 parts, 216 good, 3 rejected, job 3**, all 12 wear values preserved, energy nondecreasing. Old SQLite backup and rollback image remain private on the host; the rehearsal database was removed after verification. Rolling back later requires conversion of newer TimescaleDB state, not simply restarting the stale SQLite image.

Actual commands included `docker compose -p cnc-live -f compose.yaml -f compose.production.yaml build cnc-simulator`, a targeted `stop cnc-simulator`, `python3 /root/cnc-migrate-timescale.py /root/cnc-timescale-backup/final.sqlite cnc`, and targeted `up -d --no-deps --no-build --wait cnc-simulator`. Migration source, backups, raw full-copy validation and operator commands are retained privately on the host.

External verification: `.venv312/bin/python /tmp/cnc-live-timescale-test.py` on the deployment workstation, independent Snap7/asyncua/Paho/psycopg clients ([raw result](evidence/timescale-live-deployment.log)). HTTPS TLS and Basic auth, nonempty static assets, MQTT TLS443, S7 and encrypted OPC UA passed. All158 fields matched at sequences **36503,36506,36507**; PostgreSQL sample **36512** matched the entire independently received MQTT payload. SQL was accessed using the new restricted developer SSH account and `cnc_reader`; attempted writes and private runtime_state reads were denied. Shell sessions and forwarding to the HTTP port were denied. SQL15432 and internal protocol ports were not publicly reachable. Global SSH effective settings compared equal before/after; the new SSH Match block affects only `cnc-db`.

Public web/MQTT endpoints and credentials are unchanged. PostgreSQL is accessed through an SSH tunnel; see [connection guide](CONNECTION_GUIDE.md). No credentials were committed. **24h soak = NOT RUN**; the local short memory test below is not a live-server 24h claim. Before deployment, both Linux x86_64 GitHub CI runs passed: [branch](https://github.com/onurcankaraman/cnc-simulator/actions/runs/41408273012), [PR](https://github.com/onurcankaraman/cnc-simulator/actions/runs/41408338209).

The following local and SQLite sections are historical verification records; their statements about production not yet being changed refer to those earlier checkpoints.

---

# TimescaleDB local verification — 2026-11-02 (Europe/Istanbul)

SQLite production storage has been replaced with C++ libpq + TimescaleDB **2.31.0 / PostgreSQL 17.11**, image pinned by digest in Compose. Three local containers are healthy. Production server was not modified. Existing SQLite files remain untouched; this local database starts fresh, with no automatic import of old counters/history. **24h soak = NOT RUN.**

Environment: macOS host, Docker Desktop Linux arm64, GCC 12/C++17; libpq **15.19-0+deb12u1**; Python 3.12 independent psycopg **3.2.6**, Snap7, asyncua and Paho clients. [Runtime packages](evidence/timescale-packages.txt). Historical SQLite tests below do not establish TimescaleDB acceptance.

| Actual command | Result |
|---|---|
| `./scripts/bootstrap.sh` | PASS — generated separate ignored admin/writer/reader database credentials |
| `docker compose up -d --build --wait --wait-timeout 180` | PASS — real C++ runtime, Mosquitto and TimescaleDB; PostgreSQL host port 15432 avoids existing local 5432 service |
| `./scripts/test-all.sh` | PASS — [full log](evidence/timescale-tests.log) |
| CTest `deterministic` (real isolated `cnc_test` database) | PASS, 25.57 s — model invariants, restart states, wear/fault paths, retention and failed-transaction rollback preserving both checkpoint and events |
| CTest `bounded_history` | PASS, 6.24 s — 5,000-row cursor streaming; test enforces process peak RSS <=40,000 KiB and fetch chunk <=131,072 bytes |
| Independent S7 / encrypted OPC UA / MQTT | PASS — all 158 CSV fields at sequences 266,267,268; sizes 256/256/256/128, reads/subscriptions; writes rejected |
| `.venv312/bin/python tests/integration/timescale.py` | PASS — [SQL log](evidence/timescale-sql.log); real PostgreSQL TCP auth; all 158 persisted fields equal independent MQTT payload; reader cannot DELETE or read private runtime state; two hypertables; exact retention boundary; manual invocation of actual scheduled cleanup job deletes expired live fixture |
| `tests/integration/security.py` | PASS — anonymous MQTT / OPC None rejected; observer publishing denied |
| `tests/e2e/recovery.py` | PASS — counts, 12 tool wear values, order and energy survive restart; engine continues through broker and DB stop/start; reconnect records sample_gap |
| `tests/e2e/database_write_failure.py` | PASS — actual revoked SQL write privilege makes health false; engine progresses, checkpoint stays unchanged; grants restored in finally, persistence recovers |
| `tests/e2e/admin.py` | PASS — isolated test runtime/database; authentication, bad requests, rate limiting, E-stop and SQL audit |
| `./scripts/soak.sh 60` | Short run PASS — **60.22 seconds**, not a 24h soak |

The final Docker rebuild also includes a strict 256-event backlog guard and defaults to the runtime build stage. `scripts/smoke-connectors.sh` passed against that delivered image at sequences **504,505,506** ([log](evidence/timescale-final-connectors.log)); the 60-second resource sample below was taken on this final image.

The SQL retention test places samples just before, exactly at and after a fake 24-hour cutoff: only the first is deleted; runtime checkpoint and active order survive. The database background job is scheduled every minute and does not depend on the simulator running. Cleanup drops whole expired chunks and deletes the expired portion of the boundary chunk. Samples are not backfilled.

Three S7 + three encrypted OPC UA + three MQTT clients were active during the short resource run. [CSV](evidence/timescale-resources-60s.csv), [summary](evidence/timescale-resources-60s.json), [client log](evidence/timescale-load.log), [measured image](evidence/timescale-measured-image.txt). Sampled simulator+broker peak RSS **24.523 MiB**; PostgreSQL process RSS sum peak **122.363 MiB**; full stack process RSS sum peak **146.887 MiB**. PostgreSQL shared pages are counted in multiple processes, so the latter is not unique physical memory. The original simulator+broker ≤100 MiB target passes only this short sample; **1h / 24h acceptance NOT RUN**. Full-stack RSS is explicitly reported rather than hidden in the two-process figure. UA clients emit hostname diagnostics for the local test certificate and BadNoSubscription responses during teardown; encrypted sessions and reads succeeded.

Remaining scope: PostgreSQL-specific ENOSPC, long network blackhole, 24-hour loaded memory/liveness and production cutover/data migration are **NOT RUN**. The old SQLite tmpfs ENOSPC test was replaced by a correctly named SQL write-permission failure test; these failures are not claimed equivalent. CI workflow was updated, but remote CI for this branch is **NOT RUN** (local-only request).

Implementation references: [Timescale hypertables](https://www.tigerdata.com/docs/reference/timescaledb/hypertables/create_hypertable), [retention/chunk behavior](https://www.tigerdata.com/docs/reference/timescaledb/data-retention/add_retention_policy). Exact 24h row trimming is implemented by our cleanup function in addition to chunk removal.

---

## Historical SQLite implementation evidence (before migration)

# Actual verification report — 2026-11-01

**Implementation runs; full 24-hour acceptance is pending. 24h soak = NOT RUN.** No fake-clock run below is a live soak. No 24-hour RAM guarantee is inferred from a short measurement.

Environment: macOS host, Docker Desktop Engine 29.5.3, Linux arm64 containers; C++17/GCC 12, open62541 1.5.0, pinned Snap7 commit, libmosquitto 2.0.11, broker Mosquitto 2.1.2, SQLite 3.40.1. Independent clients: python-snap7 2.0.2, asyncua 1.1.5, paho-mqtt 2.1.0 on Python 3.12. SBOM files record exact installed package revisions. Times in raw evidence are UTC; local user date is Europe/Istanbul 2026-11-01.

## Commands and results

| ID | Actual command / method | Timestamp | Result | Measured evidence / limitation |
|---|---|---|---|---|
| T01 | `scripts/test-all.sh` → `cmake -S . -B build -G Ninja && cmake --build build -j2` in `cnc-dependencies` | 2026-11-01 | PASS | Linux C++ runtime and tests compile. [Log](evidence/test-all.log) |
| T02 | `docker compose up -d --build` | 2026-11-01 | PASS | Exactly two production services, healthy; host 127.0.0.1 ports 102/4840/1883/8080 |
| T03 | `scripts/smoke-connectors.sh` | 2026-11-01 08:20 UTC | PASS | DB1..4 lengths 256/256/256/128; all fields decoded with independent Python `struct`; slots 0 and 1 |
| T04 | Same external-client script | same run | PASS | All 158 fields matched at sequences **33409, 33410, 33411**, REAL tolerance 1e-3; encrypted UA reads + subscription and MQTT schema validation. [Delivered-image log](evidence/delivered-connectors.log) |
| T05 | C++ deterministic CTest | 2026-11-01 | PASS | 100,000 fake-clock ticks, SETUP/RUNNING/TOOL_CHANGE/IDLE and completed production |
| T06 | C++ idle-wear/accelerated-life tests | same run | PASS | No idle wear drift; 80/95/100% events; immediate hard stop at limit, including preemption of a sensor warning |
| T07 | C++ 17-fault matrix; isolated `tests/e2e/admin.py` | 2026-11-01 | PASS | Timed recovery; stop/feed interlocks; sensor warning continues NC time; admin E-stop and acknowledgement. [Log](evidence/admin.log) |
| T08 | C++ 1,000 completed-cycle stress | same run | PASS | Work-order changes and setup; job good+reject=completed; overall counters invariant |
| T09 | `.venv312/bin/python tests/e2e/recovery.py` | 2026-11-01 08:05 UTC | PASS | Real Compose stop/start: exact persisted counts/order/wear, nondecreasing energy/sequence, restart event; no incomplete part added. C++ recovery from eight states. [Log](evidence/recovery.log) |
| T10 | C++ SQLite fake-clock retention test | 2026-11-01 | PASS | Expired telemetry/events/completed orders removed at 24h+60s; active order and persistent runtime record retained |
| T11 | C++ store/HTTP external checks; `stream-memory` | same run | PASS | SQL-backed bounded history, strict query limits; 5,000 rows streamed (20,185,001 bytes), max chunk 20,185 bytes, peak process RSS **7,808 KiB**. Real HTTP 5,000-row response: 22,427,909 bytes, 1.235 s. [HTTP log](evidence/history-5000.log) |
| T12 | `tests/e2e/recovery.py` broker stop/start | 2026-11-01 08:05 UTC | PASS | Engine/DB continued; sequence advanced by 6 during outage, MQTT reconnected; retained/state subscription rechecked by protocol test |
| T13 | Live 24-hour 3×3-client soak | — | **NOT RUN** | Short loaded measurement reported separately below; not a 24h acceptance pass |
| T14 | `scripts/soak.sh 300` | 2026-11-01 08:20–08:25 UTC | PASS (shared-host measurement) | 300.11 s; 3 clients/protocol; average CPU 0.936% of one core; loaded peak RSS 23.906 MiB |
| T15 | `tests/e2e/admin.py`, `tests/integration/security.py`, connector HTTP negative tests | 2026-11-01 | PASS | Default admin 404; wrong/missing auth 401; malformed JSON/query 400; rate limit 429. OPC None session, anonymous MQTT and observer publish denied. [Security log](evidence/security.log) |
| T16 | Per-snapshot C++ contract/physics checks | 2026-11-01 | PASS | Finite ranges, cut interlocks, nondecreasing energy, power integral error <1%, XYZ derivatives; bounded spindle acceleration |
| T17 | C++ stress test | same run | PASS | 1,000 completed cycles, deterministic seeded replay, legal state transitions and valid counter/range checks |
| T18 | Real in-app browser inspection, HTTP static assets | 2026-11-01 | PASS | Live dashboard, 12 tools, six 24h-window charts, events/orders/OEE; no CDN. [Inspection](evidence/ui-inspection.txt) |
| T19 | Independent S7 `db_write`; independent UA write | 2026-11-01 08:20 UTC | PASS | Both rejected; S7 model/DB counter not overwritten. Expected Snap7 “Item not available” log is the rejection evidence |
| T20 | Compose, secret mounts, notices and source review | 2026-11-01 | PASS / scope | Non-root, cap_drop ALL, read-only rootfs, localhost bindings, generated ignored secrets, source/license notices. No claim of formal security or license certification |
| Extra | `.venv312/bin/python tests/e2e/disk_fault.py` | 2026-11-01 | PASS | Actual ENOSPC on isolated 1MiB tmpfs: `database or disk is full`; engine kept publishing; 3 failed commits recorded; freeing reserve restored writes. [Log](evidence/disk-full.log) |

## Resource measurements

The final delivered runtime completed **300.11 seconds** of loaded measurement, with **31 samples**, from 08:20:58 to 08:25:58 UTC (11:20:58–11:25:58 Europe/Istanbul). RSS counts the simulator and broker main processes from `/proc/*/status`; CPU uses both processes' user+system ticks (`CLK_TCK=100`), divided by elapsed seconds and one core. Test clients and build processes are excluded. The host is a shared Docker Desktop VM; the CPU number is normalized to one core, not a dedicated single-vCPU host benchmark. Three independent S7 clients, three encrypted OPC UA subscription clients, and three MQTT subscribers were connected. The dashboard may also poll.

`docker stats` is a separate cgroup view and is not interchangeable with process RSS: shared mappings and cache accounting differ. One actual sample was simulator 7.312 MiB / broker 3.855 MiB in that view, while process RSS summed to about 24 MiB. [Cgroup sample](evidence/cgroup-sample.txt).

| Measurement | Actual result |
|---|---:|
| Initial simulator + broker RSS | 23.891 MiB |
| Final RSS at 300.11 s | 23.730 MiB |
| Peak sampled RSS | 23.906 MiB |
| Mean process CPU, one-core normalized | 0.936% |
| Samples / interval | 31 / approximately 10 s |
| Short-run ≤100 MiB target | PASS |

[Raw process measurements](evidence/resources-5min.csv), [machine-readable summary](evidence/resources-5min-summary.json), [three clients per protocol](evidence/loaded-clients.log), [measured image ID](evidence/measured-image.txt).

Kernel high-water RSS recorded after testing: simulator **16.840 MiB**, broker **7.262 MiB**. Their sum **24.102 MiB** is a sum of individual process lifetime maxima, not a simultaneously observed combined peak. [Simulator HWM](evidence/simulator-high-water.txt), [broker HWM](evidence/broker-high-water.txt).

The small Docker init process is excluded from the two main-process RSS series; its separately captured RSS was 464 KiB in an earlier loaded sample. The final complete process tree is also recorded [here](evidence/process-rss-sample.txt). No unseen transient peak or long-run bound is asserted.
 **1h RSS = NOT RUN; 24h RSS = NOT RUN.** No sustained-leak conclusion is drawn. The SQLite file had accumulated roughly 29 MiB after several hours of runtime; this is not a 24h maximum-disk proof. WAL checkpoint/retention behavior and bounded query memory were exercised separately.

## Fixed defects and honest scope

The work initially encountered and corrected: unavailable Docker daemon; Snap7 integer handle mismatch; illegal material-wait/feed-hold/restart transitions; warning faults freezing NC time; stop-tick cutting flag inconsistency; YAML integer type mismatch; failed-commit rollback handling; oversized history materialization; overly broad secret mounts; Docker internal-network port-publishing behavior. A focused audit reproduced the original history path at 136,024 KiB peak RSS; the streamed path measured 7,808 KiB in the final C++ test. The initial streaming transport was too slow for a 5,000-row response within its 3-second deadline; bounded multi-chunk pumping corrected that, and the independent HTTP test completed all 5,000 rows in 1.235 seconds. A final hard-limit test also covers a 100% tool limit preempting an existing sensor warning.

No real PLC was connected or scanned. Python clients are independent test processes only. Unknowns remaining: full 24-hour loaded memory/liveness acceptance, field calibration, exhaustive browser/security testing. Full synthetic 24-hour plots below come from **accelerated C++ test samples**, not from real-time operation or measurements from a physical machine.

## Statistical diagnostics (synthetic, accelerated)

Actual first-24-simulated-hour traces were stratified by program, tool and 100-rpm bucket, using cutting-only samples with at least 20 observations and at least one percentage point of wear range. Sample-weighted correlations were **wear/load r=0.9346** and **wear/vibration r=0.9535**. One brass-drill stratum (program 1004, tool 8, n=21) had **wear/load r=-0.2433**; an all-strata positive-correlation pass is **not** claimed. These are model diagnostics, not a real-world realism certification. [Full group results](evidence/synthetic-correlations.json).

## Actual synthetic test figures

![Thermal/load fake-clock test](figures/thermal-load.png)
![Tool wear/vibration fake-clock test](figures/wear-vibration.png)
![Power/production fake-clock test](figures/power-production.png)
![Synthetic sample histograms](figures/histograms.png)

Reproduce figures with `scripts/plot-test-traces.py` and Matplotlib 3.10.0 after `scripts/test-all.sh`. The data source is `logs/synthetic-traces.csv` generated by the actual C++ tests. These plots are diagnostics, not a proof that synthetic data is indistinguishable from real CNC data.


## Live Docker deployment verification (2026-11-02, Europe/Istanbul)

Linux x86_64 Docker build completed, including the C++ tests. Production uses an isolated Compose project with two healthy containers; the shared reverse proxy provides authenticated HTTPS and native MQTT TLS with SNI. S7 and encrypted OPC UA were tested through SSH loopback forwards; admin remains disabled.

Actual external verification command: `.venv312/bin/python /tmp/cnc-live-test.py` (deployment workstation, independent python-snap7, asyncua and Paho clients; generated credentials fetched over SSH into memory). Result: **PASS**, all 158 CSV fields matched across S7/OPC UA/MQTT at sequences **427, 429, 430**; HTTPS history returned stored data and admin returned 404. TLS certificate verification was enabled using certifi. Authenticated GET results: `/` **200 / 1560 bytes**, `/app.js` **200 / 4339 bytes**, `/style.css` **200 / 2188 bytes**. These checks caught and corrected non-root configuration/static asset permissions in the Docker image. CI now checks that all three static assets are nonempty.

Private deployment commands, topology and raw results are retained on the host in `OPERATIONS.md` and `DEPLOYMENT_TEST.log`; credentials are excluded from Git. **24h soak = NOT RUN**; short integration checks do not establish the 24-hour RSS acceptance criterion.

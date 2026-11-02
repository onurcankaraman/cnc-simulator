# CNC Simulator — Agent working rules

**Source of truth:** `SPEC.md` and `contracts/tags.csv`; deliver entire system, no phases. This directory initially contains only the AI implementation blueprint, not a finished product.

- Use C++17 runtime + Snap7 Server + open62541 + libmosquitto MQTT publisher + bundled Mosquitto broker + TimescaleDB/PostgreSQL + static web UI. Do not replace with fake HTTP protocol stubs or Python production.
- Runtime memory target: combined simulator+broker RSS ≤100MiB over 24h (measured, not assumed). Preserve all hard requirements if optimization is needed.
- One `MachineSnapshot` source for all S7/OPC/MQTT/SQL. S7 DB1..4 byte layouts from CSV; big endian, no overlapping fields. OPC NodeId stable, MQTT schema versioned.
- Deterministic clock/seed tests; physical causality, 12 tools/wear, work orders, downtime/fault recovery, production invariants.
- 24h rolling TimescaleDB telemetry/event retention, but persistent runtime counters/wear survive reboot.
- No 3D, heavy web frameworks, CDN, Internet-exposed S7/admin, credentials in Git, or real PLC scan/control. Document synthetic origin and virtual S7 limits.
- Build then test using **independent protocol clients**; write real command/results to `docs/TEST_REPORT.md`. Do not fabricate proof. `24h soak=NOT RUN` until a real 24h test is done.
- Minimize tokens: read only relevant file portions, terse logs, no needless duplicate plans/subagents, invoke `.claude/skills/industrial-test` only at integration checkpoint.
- Follow `START_HERE.md` once; deliver working Docker Compose, quickstart README, connection guide and test report.

# START HERE — Single-run AI implementation brief

You are an autonomous software developer building the **complete executable CNC Simulator**. The current directory contains a **design blueprint, not working software**. The user wants exactly one complete delivery and no roadmap phases.

1. Read `CLAUDE.md` (or `AGENTS.md`), then `SPEC.md` sections 0, 3, 4, 7, 8, 13, and `contracts/tags.csv`. Read remaining `SPEC.md` sections only when implementing their domain. Treat the tag CSV and safety requirements as normative.
2. Implement the entire system using **production C++17**, Snap7 Server, open62541, libmosquitto, TimescaleDB, minimal native HTTP + embedded static UI. Keep the **three-container Docker Compose topology (simulator, broker, TimescaleDB)**, no Python/Node production runtime, no cloud/CDN. Python is okay only in build/test/contract generation.
3. Build the complete files specified in `SPEC.md` §11, including executable program templates, 12-tool wear, fault engine, UTC 24h SQL retention, three real protocol adapters, dashboard, authenticated/disabled-by-default admin, docs and automated tests. No mocks in production protocols; no pretending a TCP listener is OPC UA or S7.
4. Test with actual independent **S7, OPC UA and MQTT clients**, including byte-offset decoding and 1-snapshot cross-protocol equality. Test tool-wear, faults, 24h fake-clock retention, reboot recovery, disk faults, broker restart, disabled admin, safe networking. Produce `docs/TEST_REPORT.md` with **actual evidence only**.
5. Measure combined runtime RSS of simulator + MQTT broker against **≤100 MiB**. Report initial, 1h, and 24h RSS where measured; never claim an unrun 24-hour soak was successful. Try to optimize before explaining a miss, but do not remove required functionality.
6. Use short tool output (`tail`, `rg`, targeted files), and delegate to subagents only for a blocked protocol or final audit. Avoid token-heavy rewritten plans and speculative dependency searches. Do not stop at scaffolding.
7. End with exact `docker compose` commands and connection instructions; a compact PASS/FAIL/NOT RUN table and any remaining blockers. Never say the software is done unless it compiles and three external protocol clients connect.

**Non-negotiable:** Three real transports, sustained 24/7 automatic operation, tool wear, real-world causal signals, TimescaleDB rolling 24 hours, low memory, restart recovery. Public claims must identify data as synthetic; do not impersonate actual S7-1200 firmware or expose control ports to the Internet.

**Do not ask routine clarification questions.** Make documented plausible assumptions, implement, test, and disclose critical unsolved limitations. A short TODO checklist is allowed internally; **do not convert this into a phased project plan**.

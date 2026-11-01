---
name: industrial-test
description: Run final industrial protocol and realism acceptance checks with independent client libraries and evidence.
---

When the implementation is ready, execute `scripts/test-all.sh` and `scripts/smoke-connectors.sh`; capture compact logs and actual outputs. Validate S7 DB1..4 byte sizes and big-endian offsets against `contracts/tags.csv`. Independently connect OPC UA to read and subscribe, MQTT to subscribe and receive payloads. Align by `sequence` and compare across transports (float tolerance). Exercise fault injection, tool wear monotonicity, SQL window and restart. If a test is not run, state NOT RUN. Do not silently skip an unavailable protocol or a genuine 24-hour soak.

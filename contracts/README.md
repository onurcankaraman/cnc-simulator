# Contract precedence

- `tags.csv` is normative: `key`, physical unit, S7 bytes, OPC UA node id and MQTT field.
- `mqtt.schema.json` is generated from CSV. Production telemetry must include **every** CSV tag in `data`, not only the selected example keys from `SPEC.md`.
- `s7_map.md` is a human-readable projection of the CSV. S7 scalar multibyte values are big-endian, and `U8` is a full byte 0/1, not a packed PLC `BOOL`.
- `ns=2` is the intended OPC UA namespace index for the **custom** namespace; ensure registration order and verify runtime browse. All 158 exposed scalar nodes (including 12×5 slot nodes) must be read-only.
- Dynamic `meta` fields and tool object presentation may be added without breaking v1 keys/offsets. Breaking changes require `schema_version=2` and new S7 blocks.

The blueprint documents a design. End-to-end code, network and runtime conformance are **not** tested here.

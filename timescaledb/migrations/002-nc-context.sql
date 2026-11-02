-- Additive upgrade. Existing telemetry v1 payloads and counters are unchanged.
ALTER TABLE telemetry ADD COLUMN IF NOT EXISTS nc_json jsonb NOT NULL DEFAULT '{}'::jsonb;

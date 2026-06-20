# Contract Changelog

All changes to the platform contract (telemetry schema, topics, registration) are recorded here. Bump the version on any shape change and support both versions during migration.

## 2026-06-18 — account.v1 replaces state.v1 (downlink)

- `state.v1` on `as/v1/{cid}/state` is **RETIRED** — the server no longer publishes it. No fallback.
- `account.v1` on `as/v1/{cid}/account`: ONE retained payload per device carrying `tanks[]` (every tank in the account) + `default_tank_id`. Per-tank shape == old state.v1 minus the wrapper. New per-param `short` abbreviation. See [account.v1.md](account.v1.md).
- View 7 firmware (`pr_cloud`) subscribes `/account`, picks `default_tank_id` (else `tanks[0]`), renders that tank, mirrors the full tank list. `linked_at` pairing-ack semantics unchanged.

## 1.0.0 (initial)

- `telemetry.v1`: device-to-cloud payload. Multi-source array; parameters temp_f, ph, orp_mv, salinity_ppt, alk_dkh, ca_ppm, mg_ppm, no3_ppm, po4_ppm; source types tank_probes, trident, outlets, feed; `origin` live/sim guard.
- MQTT topics: `as/v1/{account}/{display}/{telemetry|state|status|cmd}`, TLS 8883, QoS 1.
- Registration v1: claim-code device-to-account binding.
- Design tokens 1.0.0: unified palette (cyan nominal, no green), device near-black + app navy surfaces, per-platform type.
## 1.1.0
- Added state.v1 downlink contract (retained full-snapshot overlay, optional delta channel reserved).
- Documented device data/command-only scope and the no-media-on-device rule.
- Added `vision` source type to telemetry for future camera-equipped devices.

# pr_data — device-independent state & logic

The "brain" shared by every display. Renders nothing; holds the live model.

**Will contain** (lifted from `PR-Desktop-Display-P4/components/data/` once stable):
- `registry.*`   — fixed-size, mutex-guarded `DeviceState` table
- `history.*`    — per-(device,value) ring buffers (+ NVS persistence)
- `thresholds.*` — per-parameter safe range + sweep, NVS-backed, reef defaults
- base `adapter.*` — generic `DeviceState` → tile projection helpers

**Excludes** layout-specific projection adapters (`reefhealth_adapter`,
`devices_adapter`, `glance_adapter`) — those describe a *particular screen* and
stay in the device repo, or move to that device's own component.

`idf_component_register(... REQUIRES pr_contract esp_timer)`

> **Migration status:** lives in `PR-Desktop-Display-P4` today. Lifted here +
> the P4 repo rewired to consume it (via `EXTRA_COMPONENT_DIRS`) as an atomic
> change, after the trade-show demo is validated. See `../../ARCHITECTURE.md`.

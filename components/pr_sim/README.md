# pr_sim — fictive reef-tank simulator (shared demo/dev source)

Populates the registry + history with believable, drifting tank data and a
looping feed cycle — so any display can run a full trade-show / bench demo with
zero networking. A shared dev tool, not shipped in field firmware.

**Will contain** (lifted from `PR-Desktop-Display-P4/components/sim/`):
- `sim_tank.*` — 9 simulated devices (Apex probes, Trident NT, pumps, ATO,
  dosers, roller, leak, LED), 24 h history seed, `pr_sim_feed()` cycle.

`idf_component_register(... REQUIRES pr_contract pr_data esp_timer)`

> **Migration status:** lives in `PR-Desktop-Display-P4` today; lifted here so
> the round display's demo reuses the exact same simulated tank. See
> `../../ARCHITECTURE.md`.

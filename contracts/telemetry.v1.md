# AquaSensei Telemetry v1

Device-to-cloud telemetry contract. The authoritative definition is `telemetry.v1.schema.json`; this document explains it and shows examples. Transport (broker, topics, QoS, cadence) is in `mqtt-topics.md`.

## Shape

A device publishes one JSON message per poll containing one or more `sources`. A single-source device (Trident) sends one entry; an aggregator (View 7) sends several. Top-level fields:

| field | type | notes |
|---|---|---|
| `schema` | const | always `as.telemetry.v1`; the app routes by this |
| `client_id` | string | `pr-<mac>`, lowercase, no colons |
| `device_type` | string | model name (trident, desktop7, roller_mat, monitor) |
| `ts` | integer | Unix epoch seconds, device clock (NTP-synced) |
| `fw_version` | string | reporting firmware version |
| `sources` | array | one entry per data source |

## Parameters (units are in the key name)

| key | unit | key | unit |
|---|---|---|---|
| `temp_f` | degrees F | `alk_dkh` | dKH |
| `ph` | pH | `ca_ppm` | ppm |
| `orp_mv` | mV | `mg_ppm` | ppm |
| `salinity_ppt` | ppt | `no3_ppm` | ppm |
| | | `po4_ppm` | ppm |

Omit a key rather than sending a zero or placeholder. To add a parameter, add it to the schema and bump the version.

## Source types

- `tank_probes`: probe chemistry (temp, pH, ORP, salinity)
- `trident`: alk, Ca, Mg, plus `meta` (status, error_code, reagent_ml, last_cal_days)
- `outlets`: array of `{name, state}` where state is ON/OFF/AON/AOF/PROFILE
- `feed`: `{active, cycle}`

## The sim rule

Demo / simulator sources must set `origin: "sim"`. The cloud rejects sim telemetry so it can never be ingested as real. Live sources default to `origin: "live"`.

## Examples

Trident (single source):

```json
{
  "schema": "as.telemetry.v1",
  "client_id": "pr-8cfd490d50d8",
  "device_type": "trident",
  "ts": 1733864400,
  "fw_version": "1.1.0",
  "sources": [
    { "source_id": "apex:trident", "type": "trident", "origin": "live",
      "readings": { "alk_dkh": 8.4, "ca_ppm": 430, "mg_ppm": 1320 },
      "meta": { "status": "idle", "error_code": 0, "reagent_ml": [21.0, 19.5, 18.0], "last_cal_days": 12 } }
  ]
}
```

View 7 (multi-source):

```json
{
  "schema": "as.telemetry.v1",
  "client_id": "pr-8cfd490d50d8",
  "device_type": "desktop7",
  "ts": 1733864400,
  "fw_version": "0.1.0-dev",
  "sources": [
    { "source_id": "apex:AC5-12345:tank", "type": "tank_probes", "origin": "live",
      "readings": { "temp_f": 77.9, "ph": 8.12, "orp_mv": 380, "salinity_ppt": 35.0 } },
    { "source_id": "apex:AC5-12345:trident", "type": "trident", "origin": "live",
      "readings": { "alk_dkh": 8.4, "ca_ppm": 430, "mg_ppm": 1320 } },
    { "source_id": "apex:AC5-12345:outlets", "type": "outlets", "origin": "live",
      "outlets": [ { "name": "Return", "state": "ON" }, { "name": "Skimmer", "state": "ON" } ] },
    { "source_id": "apex:AC5-12345:feed", "type": "feed", "origin": "live",
      "feed": { "active": false, "cycle": null } }
  ]
}
```

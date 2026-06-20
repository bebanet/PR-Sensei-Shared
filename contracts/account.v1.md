# account.v1 — server-to-device account state (downlink)

Replaces `state.v1` as of 2026-06-18. The server publishes **one retained
payload per device** to `as/v1/{account}/{display}/account` (QoS 1, retained,
JSON), carrying **every tank in the owner's account** plus a `default_tank_id`
preference. Same broker / identity / retained-JSON pattern as `state.v1` — the
device just subscribes one topic deeper and the per-tank shape moved one level
down inside `tanks[]`.

**Display state, never commands.** The device renders this and actuates on
nothing. No `commands`/`actions` field belongs here — that would need a separate
command topic with its own contract.

> NOTE: the canonical JSON Schema lives at `contracts/account.v1.schema.json` in
> the AquaSensei repo. Vendor it alongside this doc when that repo is checked out
> on the build host; the firmware parser (`components/pr_cloud/pr_cloud.cpp`)
> already validates defensively against the fields below.

## Topic

`as/v1/{account}/{display}/account` — `{account}/{display}` is the device `cid`,
byte-equal to the telemetry publish topic's middle segments.

## Top level

| field             | type                   | notes                                                              |
| ----------------- | ---------------------- | ------------------------------------------------------------------ |
| `schema`          | `"as.account.v1"`      | Must equal exactly. Reject otherwise.                              |
| `ts`              | number (epoch seconds) | When the server built the payload.                                 |
| `tanks`           | array (REQUIRED)       | Every tank in the account. `[]` is valid → render "No tanks yet".  |
| `default_tank_id` | string \| null         | "Show this tank first" preference. May be null.                    |
| `device_type`     | string (optional)      | Echoes the recipient's class slug, e.g. `"desktop7"`.              |
| `linked_at`       | number (optional)      | Present ONLY on the pairing-ack push. Fires the one-time "Linked". |

## Per tank (`tanks[]`)

| field           | type                                                  |
| --------------- | ----------------------------------------------------- |
| `id`            | string (REQUIRED)                                     |
| `name`          | string (REQUIRED)                                     |
| `score.value`   | int 0–100 (REQUIRED)                                  |
| `score.band`    | `excellent`/`good`/`fair`/`poor`/`unknown` (REQUIRED) |
| `headline`      | string — calm one-liner, render verbatim              |
| `display_units` | `{ temp: "F"\|"C", volume: "gal"\|"L" }`              |
| `params[]`      | array of param entries (below)                        |

## Per param (`tank.params[]`)

| field             | type                                                                                  |
| ----------------- | ------------------------------------------------------------------------------------- |
| `key`             | string (REQUIRED) — `alk`/`calcium`/`magnesium`/`nitrate`/`phosphate`/…               |
| `value`           | number (REQUIRED)                                                                      |
| `status`          | `ok`/`low`/`high`/`off`/`tracking` (REQUIRED)                                          |
| `label`           | string — display name                                                                 |
| `short`           | string — tight-space abbreviation (`ALK`, `Ca`, `NO₃`, `PO₄`); fall back to `label`.  |
| `unit`            | string — canonical unit, empty for pH                                                  |
| `target_low`      | number — in-band check                                                                 |
| `target_high`     | number                                                                                 |
| `source`          | `apex`/`hydros`/`manual`/`hanna`/`strip`/`refract`/`icp`/`computed`                    |
| `last_reading_at` | number (epoch s) — when read at source; device picks its own staleness threshold.     |

## Device behaviour

- **Which tank:** pick the entry whose `id === default_tank_id` if set and
  present; else `tanks[0]`. Single-tank focus (a picker is future work).
- **Empty list** (`tanks: []`): render a quiet "No tanks yet".
- **Unknown enums** (e.g. an unrecognised `band`): render defensively, no crash.
- **Retained + push only:** the broker delivers the last retained payload on
  SUBSCRIBE; the server pushes a fresh one on change. No polling, no local cache
  that survives reboot.

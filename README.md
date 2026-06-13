# aquasensei-shared

The single source of truth for the AquaSensei platform. Every device firmware and the app conform to what is defined here. If a fact about the contract or the design system lives anywhere else, it is a copy and it will drift. It lives here.

## What is in here

```
contracts/                  the language-neutral platform contract
  telemetry.v1.schema.json  device-to-cloud payload (machine-checkable, authoritative)
  telemetry.v1.md           human spec + examples for the above
  mqtt-topics.md            topic structure, QoS, cadence, broker
  registration.v1.md        claim-code / device-to-account binding flow
  CHANGELOG.md              contract version history

design/                     the design system
  tokens.json               colors + type (edit THIS)
  generate-tokens.mjs        emits tokens.h and tokens.css from tokens.json
  tokens.h                  GENERATED: firmware / LVGL color defines
  tokens.css               GENERATED: app CSS variables

schema/                     the C side (compiled into firmware via submodule)
  include/schema.h          DeviceState + enums
  pr_espnow_wire.h          ESP-NOW binary wire
```

## Two kinds of shared, two forms

- **Code** (`schema/`): C headers, compiled into firmware via git submodule. The Node app cannot use these.
- **Contract** (`contracts/`, `design/`): language-neutral, because both the C firmware and the JS app implement it. The JSON Schema and the generated CSS/header are the bridge across the C/JS boundary.

## The token generator

`design/tokens.json` is the only file anyone edits. Run the generator to produce the consumable outputs:

```
node design/generate-tokens.mjs
```

This writes `design/tokens.h` (firmware) and `design/tokens.css` (app). The generated files are committed so consumers can use them directly. Never hand-edit `tokens.h` or `tokens.css`. Change a color in `tokens.json`, regenerate, commit.

## How consumers use this

- **Firmware** (Trident, View 7 / P4, roller mat): add this repo as a git submodule, `#include` `design/tokens.h`, and serialize telemetry to `contracts/telemetry.v1.schema.json`. Replace local color `#define`s with the `AS_COL_*` names.
- **App**: consume `contracts/telemetry.v1.schema.json` and validate every inbound MQTT payload against it at the ingest boundary (reject off-contract messages, do not write them to `readings`). Import `design/tokens.css` into the theme.

## Versioning rule

The contract is versioned (`as.telemetry.v1`). The data shape is what the version tracks, not the device roster. Adding a new device of an existing shape does not bump anything. Adding a parameter or a new source shape is a contract change: update the schema, bump the version, add a CHANGELOG entry, and support both versions during migration. Devices declare their version in the `schema` field; the app routes and validates by it.

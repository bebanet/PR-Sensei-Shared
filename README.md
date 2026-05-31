# PR-Sensei-Shared

The **single source of truth** for the PrintedReef ecosystem — the data
contract every firmware repo must agree on byte-for-byte, and (increasingly)
the device-independent logic and UI widgets they share.

> **Read [`ARCHITECTURE.md`](ARCHITECTURE.md) first.** It is the canonical
> "one brain, many faces" model, the pinned platform (native ESP-IDF, LVGL
> version), the component layout, the consumption recipe, and the brand /
> transport rules every repo follows.

## Contents

- `include/schema.h` — the unified device data model (`DeviceState`).
  How a device's data lives in memory on a display.
- `include/pr_espnow_wire.h` — the ESP-NOW wire format. The packed,
  <=250-byte binary encoding of a `DeviceState` for peer-to-peer
  transmission. Includes `schema.h`.
- `components/` — the shared core as ESP-IDF components:
  - `pr_contract` — the two headers above, exposed as a `REQUIRES`-able
    component (live now).
  - `pr_data` · `pr_ui` · `pr_sim` — device-independent state/logic, reusable
    LVGL widgets, and the demo simulator. Being lifted from
    `PR-Desktop-Display-P4`; see each README and `ARCHITECTURE.md` §6.

## How it's used

Consumed as a **git submodule**, never copied. Repos that depend on it:

- `PR-Roller-Mat-Filter` — encodes telemetry (`pr_wire_encode_telemetry`)
- `printedreef-display` — decodes telemetry (`pr_wire_decode_telemetry`)
- future first-party device and display repos

Add to a consuming repo:

    git submodule add https://github.com/bebanet/PR-Sensei-Shared.git lib/PR-Sensei-Shared

Include path: `#include "PR-Sensei-Shared/include/schema.h"`

## Changing these files

These are a **contract**. A change here ripples to every device.

- `schema.h` is locked at schema version 1. Any field change bumps
  `PR_SCHEMA_VERSION`; the wire format's version checks depend on it.
- Changing the wire layout bumps `PR_WIRE_VERSION`.
- After any change, each consuming repo must update its submodule
  pointer deliberately and be recompiled — pin versions, don't drift.

## Versioning

Tag releases (`v1.0.0`, ...). Consumers pin to a tag so a contract
change is an explicit, reviewed submodule bump, never a silent one.

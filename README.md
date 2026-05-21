# PR-Sensei-Shared

Shared contract for the PrintedReef ecosystem. Two headers that every
firmware repo must agree on byte-for-byte.

## Contents

- `include/schema.h` — the unified device data model (`DeviceState`).
  How a device's data lives in memory on a display.
- `include/pr_espnow_wire.h` — the ESP-NOW wire format. The packed,
  <=250-byte binary encoding of a `DeviceState` for peer-to-peer
  transmission. Includes `schema.h`.

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

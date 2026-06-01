# PrintedReef — Shared Core & Multi-Device Architecture

This repo (`PR-Sensei-Shared`) is the **single source of truth** for everything
that is *not* specific to one device: the data contract, the device-independent
logic, and the reusable UI building blocks. Every PrintedReef firmware repo
consumes this one, so the whole fleet stays byte-identical on the wire and
consistent in behaviour and look.

> **Mental model: one brain, many faces.**
> The *brain* (contract + data + widgets) lives here, written once and versioned.
> Each product is a thin *face* — its panel driver, its screen layout, its `main`.
> Sensor devices are *data producers* that speak the contract. A new product is a
> new face, not a new brain.

---

## 1. The layers (components in this repo)

```
pr_contract   schema.h · pr_espnow_wire.h          the wire/data contract
pr_data       registry · history · thresholds      device-independent state + logic
pr_ui         theme tokens + widgets               gauge · stat card · checklist · tile · sparkline
pr_sim        fictive-tank simulator               shared demo/dev data source
pr_net*       apex · hydros · esp-hosted glue       transports (later)
```

Dependency direction (no cycles):
`pr_ui` → `pr_contract`; `pr_data` → `pr_contract`; device app → `pr_ui` + `pr_data` (+ `pr_sim` for demos).

**What stays in the device repo (the "face"):** the panel/touch driver, the
exact palette tuning (IPS vs OLED), the screen layout, the per-layout projection
adapter, and `main`.

> Migration status: `pr_contract` is live (wraps `include/`). `pr_data` / `pr_ui`
> / `pr_sim` currently live inside `PR-Desktop-Display-P4` and are lifted here as
> they stabilise — see each component's README and §6.

---

## 2. The core principle (from the feasibility doc)

PrintedReef is an **observation layer**, not a controller and not an app
replacement. Every screen renders **only from `DeviceState`**; transports
(Apex HTTP, ESP-NOW, Hydros cloud) normalise *into* it. That single abstraction
is what makes a new device or display cheap: write a new transport or a new
view, reuse the brain.

- **Monitoring is the product.** Read-only fully serves the mission.
- **Tag transport honestly.** Apex / Profilux = LAN (local). Hydros = cloud.
  PrintedReef gear = ESP-NOW (direct). The stale indicator reflects the difference.
- **Control is off by default** (v1.5, per-capability opt-in + liability disclaimer).
- **Never green.** Status colours only: teal = in-range, amber = attention,
  red = alert. Cyan is a neutral accent. Ocean brand.

---

## 3. Platform — pinned, identical across every repo

| Item | Value |
|------|-------|
| Framework | **native ESP-IDF** (not Arduino) |
| IDF version | **5.3.3** today → bump the whole fleet to **5.5 at the start of the Wi-Fi/ESP-Hosted work** (better C6 `esp_wifi_remote` support) |
| UI | LVGL 9.5 |
| Targets | one IDF builds them all: `esp32` · `esp32s3` · `esp32c6` · `esp32p4` (`idf.py set-target`) |

Pin every repo to the same IDF version so the shared contract compiles
identically everywhere. Bump the fleet together, never one repo at a time.

---

## 4. How a device repo consumes the shared core

**Recommended: git submodule + `EXTRA_COMPONENT_DIRS`** (least moving parts;
the submodule working tree doubles as the live dev checkout).

```bash
git submodule add https://github.com/bebanet/PR-Sensei-Shared.git shared
git submodule update --init --recursive
```

Top-level `CMakeLists.txt`:
```cmake
cmake_minimum_required(VERSION 3.16)
list(APPEND EXTRA_COMPONENT_DIRS shared/components)   # the one line
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(my_device)
```

Then a device component just declares what it needs:
```cmake
idf_component_register(SRCS "main.c" INCLUDE_DIRS "." REQUIRES pr_ui pr_data pr_contract)
```

**Alternative: component-manager git dependency** (cleaner semver, no submodule) —
in `main/idf_component.yml`:
```yaml
dependencies:
  pr_ui:   { git: "https://github.com/bebanet/PR-Sensei-Shared.git", path: components/pr_ui,   version: "^1.0.0" }
  pr_data: { git: "https://github.com/bebanet/PR-Sensei-Shared.git", path: components/pr_data, version: "^1.0.0" }
```

---

## 5. Dev loop & versioning

- **Edit shared + device together:** with the submodule, `shared/` *is* a working
  checkout — edit, rebuild the device immediately, commit in the submodule, then
  bump the parent (`git add shared && git commit`). With the component manager,
  use `override_path:` to point at a local checkout during development.
- **Pin = submodule commit (Option A) or semver git tag (Option B).** Bump deliberately.
- **`PR_SCHEMA_VERSION`** in `schema.h` gates the contract; bump it on any wire change.
- **CI build-matrix** is what keeps the fleet honest: build *every* device app
  (`idf.py set-target …`) against each shared change, so a contract edit that
  breaks the roller mat or the round display fails immediately.

---

## 6. Devices in the fleet

| Device | Chip | Display | Consumes |
|--------|------|---------|----------|
| Desktop Display 7″ | ESP32-P4 | EK79007 MIPI-DSI 1024×600 (IPS) | pr_contract · pr_data · pr_ui (+ pr_sim) |
| Round display 1.75″ | TBD (likely ESP32-S3) | AMOLED round, QSPI | pr_contract · pr_data · pr_ui (own round layout + OLED palette) |
| Sensor devices (roller mat, ATO, doser…) | ESP32 / S3 / C6 | none | pr_contract (produce `DeviceState`, send over ESP-NOW) |

---

## 7. Branch / publish discipline

- Shared-core changes land on a **branch** and are reviewed before `main`, because
  `main` is what every repo pulls — a breaking change to `main` breaks the fleet.
- Tag releases (`v1.0.0`, …) so repos can pin a known-good shared version.

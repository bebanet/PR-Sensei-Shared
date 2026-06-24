# as_core — device-agnostic AquaSensei connection core

The shared wire stack. Every AquaSensei display device talks to the cloud through
this one component, so telemetry and pairing can never drift between devices.

**Contains:**
- MQTT-over-TLS:8883 transport to AWS IoT Core (X.509 mutual-cert auth), with
  reconnect, `/account` re-subscribe on every connect, and a subscribe-rejection
  backoff that keeps telemetry alive while `/account` is denied
- device identity — `pr_cloud_get_cid()` → `{account}/{client_id}` (client_id is
  the MAC-derived `pr-<12hex>`, also the AWS IoT thing name); the same cid feeds
  the telemetry topic, the `/account` subscribe, and the pairing QR (no drift)
- `telemetry.v1` uplink — the envelope (schema/client_id/device_type/ts/fw_version)
  + publish + cadence (fresh-data or 5-min heartbeat)
- `account.v1` downlink — subscribe, cJSON decode, schema + range validation, the
  `linked_at` pairing-ack gate (NVS), multi-tank state + sticky tank selection
- `pr_cloud_build_claim_url()` — the AquaSensei claim deep-link

**Device-agnostic by construction.** `REQUIRES` only ESP-IDF components — no Wi-Fi
driver, no sensor registry, no renderer. The device's shell ("face") injects its
specifics through seams, registered before `pr_cloud_start()`:
- `pr_cloud_set_net_provider({is_online, mac})` — feed your Wi-Fi's state + STA MAC
- `pr_cloud_set_telemetry_provider({build_sources, secs_since_sync})` — your shell
  builds every telemetry source (as_core only builds the envelope)
- `pr_cloud_set_device_type("<slug>")` — e.g. `"desktop7"`, `"trident175"` — the
  ONE device string, mapped to `telemetry.device_type`
- `pr_cloud_set_state_cb(cb)` — your renderer receives the parsed `account.v1`
  state (no JSON in the renderer); marshal LVGL work under your display lock

**Excludes** (lives in the device repo): the renderer that draws the `account.v1`
state, the Wi-Fi onboarding UI, the pairing QR screen, and the device's reading
sources. as_core hands over data + events; the face draws.

**Per-device secrets** — `aws_device_cert.pem`, `aws_private_key.pem`,
`aws_root_ca.pem`, and `cloud_secrets.h` are **gitignored**. Each device drops its
own into this folder (copy `cloud_secrets.h.example`); they are never committed
here. A future Fleet-Provisioning flow moves these to NVS at first boot.

`idf_component_register(... REQUIRES mqtt esp-tls json nvs_flash esp_timer esp_netif esp_app_format)`

> Public header is `pr_cloud.h` (symbols `pr_cloud_*`); a rename to `as_*` is a
> deferred cosmetic. Lifted from `PR-Desktop-Display-P4` (was
> `components/pr_cloud`). Worked example of a shell consuming this: that repo's
> `main/as_shell.cpp` (the net + telemetry providers) and `main.cpp` wiring. See
> `../../ARCHITECTURE.md`.

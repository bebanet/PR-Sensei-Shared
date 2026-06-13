# MQTT Topics and Transport v1

## Broker

AquaSensei's own broker, separate from CoralVue's HYDROS cloud. Recommended: AWS IoT Core, in AquaSensei's AWS account. Alternatives the team left open: EMQX, HiveMQ, self-hosted Mosquitto. Both displays must use the SAME broker.

- Transport: MQTT over TLS, port 8883
- Device auth: X.509 mutual TLS, per-device client certificate (AWS IoT standard). For a self-hosted Mosquitto test, username/password over TLS is acceptable but the device auth code differs.

## Identity

- `clientId` = `pr-<mac>`, lowercase, no colons (from the device STA MAC).
- `{display}` in topics = the clientId.
- `{account}` = the account the device is bound to (see `registration.v1.md`). Before registration, a placeholder dev account id is used for pipe testing.

## Topics

```
as/v1/{account}/{display}/telemetry   device -> cloud   readings (telemetry.v1)
as/v1/{account}/{display}/state       device -> cloud   current display state (optional)
as/v1/{account}/{display}/status      device -> cloud   presence (LWT)
as/v1/{account}/{display}/cmd         cloud  -> device   commands (subscribe)
```

## QoS, retain, cadence

- `telemetry`: QoS 1, not retained. Publish on each successful poll that yields fresh data, plus a heartbeat at least every poll interval (Trident ~10 min idle, View 7 ~5 min idle, faster during a test or feed).
- `cmd`: QoS 1, subscribe. In step 1, log only; do not actuate.
- `status`: set a Last Will and Testament on connect (for example `{"online": false}`) so the cloud sees ungraceful disconnects. Publish `{"online": true}` retained on connect.

## Offline behavior

The cloud is additive. If the broker is unreachable, the device keeps polling its controller and rendering. The MQTT client retries with backoff. Telemetry buffered or dropped while offline is a device choice; for v1, dropping is acceptable since the cloud down-samples anyway.

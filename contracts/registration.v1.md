# Device Registration v1 (claim-code binding)

This is the spec for the step that turns the placeholder `{account}` into a real device-to-account binding. It is the next build step after the publish pipe, and it is built as a matched pair: app + firmware. Documented here so both sides implement the same flow.

## Goal

Bind a physical device (`clientId pr-<mac>`) to a user account and a tank, so its telemetry lands under that account and the app can address it.

## Flow

1. Device generates or displays a short **claim code** (for example 6 alphanumeric chars) on its screen, tied to its `clientId`. Screenless devices expose it over their setup portal.
2. User signs into the app, opens "Add a device," and enters the claim code.
3. App validates the code, creates a `devices` row binding `client_id` to the user's account, and lets the user assign it to a tank.
4. App returns the `account` id to the device (over its authenticated setup channel, or the device polls a claim-status endpoint).
5. Device stores `account` in NVS and switches its publish topics from the placeholder to `as/v1/{account}/{display}/...`.

## Credential model

- v1: the device already holds its X.509 cert from manual or fleet provisioning. Registration binds identity to account at the application layer; it does not issue the TLS cert.
- The AWS IoT policy is scoped per `clientId`, so a device can only publish under its own `{display}` segment regardless of `{account}`. The app enforces that `{account}` matches the bound device.

## App: devices table (minimum columns)

| column | notes |
|---|---|
| `client_id` | `pr-<mac>`, primary identity |
| `user_id` / `account_id` | owner |
| `tank_id` | assigned tank (nullable until assigned) |
| `device_type` | from telemetry |
| `claim_code` | one-time, expires |
| `status` | `pending` -> `claimed` -> `active` |
| `cert_id` | AWS IoT cert reference (if app-managed) |
| `fw_version` | last reported |
| `last_seen` | from telemetry / status LWT |

## State machine

`pending` (cert exists, unclaimed) -> `claimed` (bound to account, awaiting first telemetry) -> `active` (telemetry flowing). Unbinding returns to `pending`.

## Out of scope for v1

Fleet provisioning automation, certificate rotation, and transfer of a device between accounts. Manual cert per device is fine until volume justifies fleet provisioning.

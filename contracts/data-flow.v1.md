# AquaSensei Data Flow v1

How data moves between a device, the cloud, and the app, and what is allowed on each path. This is the counterpart to `telemetry.v1` (the uplink) and `mqtt-topics.md` (the transport). It defines the downlink (`state.v1`), the device data scope, and how media and camera vision fit.

## Principles
1. **The device owns the readings it collects.** It read the Apex itself, so it renders temperature, alk, pH, and the rest immediately from local memory. The cloud is never in the loop for displaying a reading. The screen works even if the cloud is slow or offline.
2. **The cloud owns the intelligence.** Grades (ok / low / high), recommendations, coaching text, and alerts are computed by the engines and sent down. The device renders its local readings and overlays the intelligence on top.
3. **Send only what changed.** The downlink carries only the intelligence layer, and only when it changes. A poll where nothing meaningful changed sends nothing down.
4. **The device channel is data and commands only.** Never media.

## Uplink (device to cloud)
Defined by `telemetry.v1`. The device publishes its readings (and, for a future vision-capable device, observations) on `.../telemetry`. See `telemetry.v1.md`.

## Downlink (cloud to device): state.v1
Topic: `as/v1/{account}/{display}/state`, retained.

The cloud publishes the current intelligence overlay as a **retained full snapshot, updated only when it changes**. Because the message is retained, any device that connects or reconnects immediately receives the latest complete overlay, then nothing more until something changes. This is self-healing by construction: the retained snapshot is always the full truth, so a rebooted device is never left guessing.

Snapshot shape:
```json
{
  "schema": "as.state.v1",
  "type": "snapshot",
  "ts": 1733864460,
  "overlay": {
    "apex:AC5-12345:tank": {
      "alk_dkh": { "grade": "low", "rec": "Alk 7.2 dKH, below your 8.0-9.0 target." },
      "ph":      { "grade": "ok" },
      "temp_f":  { "grade": "ok" }
    }
  },
  "alerts": [ { "id": "skimmer_full", "sev": "warn", "text": "Skimmer cup ~82% full" } ],
  "coaching": "Alkalinity is trending down over the last 3 days."
}
```

The `overlay` is keyed by the same `source_id`s the device sent up, so the device matches intelligence to readings it already holds. The overlay is small (a handful of params plus a few alerts, roughly 1 to 2 KB), so it fits in one MQTT message.

**Why a full snapshot instead of diffs:** for a payload this small, the bandwidth saved by diffs is negligible, and diffs force the device to reconcile a snapshot plus a delta stream, which is more code and more failure modes. Full-snapshot-on-change keeps the "only send what changed" intent (you only publish when something changes) while staying simple and self-healing. If overlays ever grow large, an optional non-retained `.../state/delta` channel can carry incremental updates, with `state` still holding the authoritative snapshot for reconnect. For v1, snapshot-only.

## Commands (cloud to device): cmd
Topic: `as/v1/{account}/{display}/cmd`, QoS 1, subscribe.

User adjustments made in the app that affect the device are commands, and they are small data, so they sync down here: changing a target or threshold, acknowledging an alert, or asking the device to run a Trident test. JSON, sub-second, effectively free.

**Control is different from display.** Anything that makes the device DO something (run a test, change a target that affects dosing) is a control action and must be authenticated and safe, not merely delivered. In step 1 the device logs commands and does not actuate. Actuation, especially anything touching dosing or the Apex, is a later, deliberate step.

## Device scope: data and commands only, never media
The device channel and the MQTT broker carry small JSON only. They never carry images or video. This is a hard limit, not a preference:
- MQTT bills per message in 5 KB units and caps payload at 128 KB. A single photo is hundreds of messages and a video is thousands, and a real photo exceeds the size cap outright.
- The screens are instrument panels (466 px round, or 1024x600), not photo frames. Media belongs where there is a real screen and real storage: the app.

So media never reaches a device. Settings and commands, being small data, do.

## Media: app-side only
Photos and video live entirely in the app (Postgres plus the media volume today) and are shown in the app UI on a phone or browser. Nothing media-shaped is sent to a device or over the broker. A user adjusting media in the app is an app concern with no device involvement.

## Camera and vision (the ReefMind pattern)
A camera is two different things, and only one touches the data path.

**The image stays app-side.** A video stream or a captured frame is heavy and never goes to a device or over the broker.

**What the image tells you is data.** Vision analysis turns a frame into a few small observations: skimmer cup fullness, water level, coral color shift, equipment state. Those are bytes, they flow like any other reading, and they feed the same engines.

Two ways vision can run:
- **App-side vision (near-term, recommended):** the app analyzes frames it pulls or receives (it already runs Claude Vision for OCR and photo-diff). The observations are generated in the cloud, feed the engines, and surface as alerts in the downlink and in the app UI. They do not traverse the uplink.
- **Device-side vision (future, a camera-equipped device):** the device runs detection and publishes observations on the uplink using the `vision` source type in `telemetry.v1`. Reserved now so the contract is ready.

Device-side vision observation shape:
```json
{ "source_id": "cam:tank-front", "type": "vision", "origin": "live",
  "observations": [
    { "key": "skimmer_cup_fullness_pct", "value": 82, "confidence": 0.9 },
    { "key": "water_level", "value": "low", "confidence": 0.7 }
  ] }
```
Observations never carry the frame. If a finding references an image, it references where the app stored it, never the bytes.

**Vision alerts on the screen.** A vision-derived alert ("skimmer cup ~82% full") is small text, so it rides the normal `state` downlink and the controller screen can show it, while the frame itself never goes near the device.

**Live video to watch.** If AquaSensei ever offers live-video viewing, that is a separate app-side streaming feature on its own video pipe (the camera or its vendor cloud to the user's app), never through the broker or a device. It is optional polish, distinct from the vision analysis above, which is the part that carries product value.

## Cost note
Because readings render locally, the downlink sends only a small overlay on change, and media never touches the broker, per-device message volume stays tiny (pocket change per device per month at any reasonable poll cadence). Media cost lives in the app's own storage and any streaming infrastructure, entirely separate from the device channel.

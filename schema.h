#ifndef PRINTEDREEF_SCHEMA_H
#define PRINTEDREEF_SCHEMA_H

#include <stdint.h>

// PrintedReef unified device schema  -  schema version 1
//
// The canonical in-memory data model shared by every transport.
// Apex (HTTP JSON), ESP-NOW (packed binary), and cloud all normalize
// INTO this struct; the UI renders only from it and never parses a
// transport payload directly.

#define PR_SCHEMA_VERSION   1

// ---- fixed sizes (bounded; no heap allocation on the MCU) ----
#define PR_MAX_SECONDARY    6     // secondary values per card
#define PR_MAX_ALERTS       4     // active alerts per device
#define PR_ID_LEN          32     // device_id / source_id
#define PR_NAME_LEN        24     // user-facing device name
#define PR_GROUP_LEN       20     // tank / system grouping name
#define PR_KEY_LEN         16     // value machine key
#define PR_LABEL_LEN       16     // value display label
#define PR_UNIT_LEN         8     // value unit string
#define PR_CODE_LEN        16     // alert machine code
#define PR_MSG_LEN         48     // alert human-readable message

// ---- enums (stored as uint8_t; stable numbering for the wire format) ----

enum DeviceType : uint8_t {
  DEV_GENERIC         = 0,
  DEV_APEX_CONTROLLER,    // the Apex itself (outlet panel - v1.5)
  DEV_TRIDENT,            // alk / ca / mg
  DEV_TANK_PROBES,        // temp / pH / salinity / ORP
  DEV_ROLLER_MAT,         // fleece filter
  DEV_ATO,                // auto top-off
  DEV_SKIMMER,            // protein skimmer
  DEV_ENVIRONMENT,        // room puck: CO2 / temp / humidity / VOC
};

enum Transport : uint8_t {
  TRANSPORT_NONE      = 0,
  TRANSPORT_ESPNOW,       // peer-to-peer; primary for first-party hardware
  TRANSPORT_WIFI_LAN,     // over the router; first-class, not a fallback
  TRANSPORT_CLOUD,        // AquaSensei (upload-only in v1)
};

enum DeviceStatus : uint8_t {
  STATUS_OFFLINE      = 0,
  STATUS_ONLINE,
  STATUS_STALE,           // last update too old; data suspect
  STATUS_SWITCHING,       // changing transports - do NOT treat as offline
  STATUS_PAIRING,
  STATUS_ERROR,
};

enum ValueState : uint8_t {
  VS_UNKNOWN          = 0,
  VS_OK,
  VS_WARN,
  VS_ALARM,
};

enum AlertSeverity : uint8_t {
  SEV_INFO            = 0,
  SEV_WARN,
  SEV_ALARM,
};

// ---- value-level structs ----

// One measured value. The adapter assigns `state` (using source alarms
// where available, else display-side thresholds). `decimals` is a render
// hint so the UI never needs to know per-parameter precision.
struct ValueEntry {
  char       key[PR_KEY_LEN];      // e.g. "alk", "temp"
  char       label[PR_LABEL_LEN];  // e.g. "Alkalinity"
  float      value;
  char       unit[PR_UNIT_LEN];    // e.g. "dKH", "ppm", "F"
  uint8_t    decimals;             // display precision hint
  ValueState state;
  bool       valid;                // false = reading unavailable / empty slot
};

struct Alert {
  AlertSeverity severity;
  char          code[PR_CODE_LEN];     // machine code, e.g. "TEMP_HIGH"
  char          message[PR_MSG_LEN];   // human-readable, optional
  uint32_t      since_ms;              // local millis() when first raised
};

// ---- the canonical device record (one per card) ----

// device_id / source_id namespacing convention:
//   apex:<serial>:<group>      e.g. "apex:1A2B3C:trident"
//   pr:<type>:<mac-suffix>     e.g. "pr:rollermat:a4cf12"
//   cloud:<id>
//
// Fan-out: one Apex SOURCE produces several DeviceState records that
// share a single source_id but carry distinct device_ids. The paired-
// source soft cap counts SOURCES, not cards.
struct DeviceState {
  uint8_t      schema_version;         // == PR_SCHEMA_VERSION

  char         device_id[PR_ID_LEN];   // globally unique, namespaced
  char         source_id[PR_ID_LEN];   // the paired source it came from
  char         group[PR_GROUP_LEN];    // tank/system, e.g. "Display Tank"

  DeviceType   device_type;
  char         name[PR_NAME_LEN];      // user-facing, editable
  Transport    transport;              // how the latest data arrived
  DeviceStatus status;

  uint32_t     last_update_ms;         // local millis() at last update;
                                       // staleness = (millis() - this)
  int8_t       rssi;                   // link quality; 0 if not applicable

  ValueEntry   primary;                // the hero value
  ValueEntry   secondary[PR_MAX_SECONDARY];
  uint8_t      secondary_count;

  Alert        alerts[PR_MAX_ALERTS];
  uint8_t      alert_count;

  // controls[] reserved for v1.5 outlet/device control - intentionally
  // omitted until then so v1.5 stays purely additive (no schema bump).
};

#endif  // PRINTEDREEF_SCHEMA_H

#ifndef PR_ESPNOW_WIRE_H
#define PR_ESPNOW_WIRE_H

#include <stdint.h>
#include <string.h>
#include "schema.h"

// PrintedReef ESP-NOW wire format  -  wire protocol version 1
//
// Compact, variable-length binary encoding of a DeviceState for
// transmission over ESP-NOW (hard limit: 250 bytes per packet).
//
// A DeviceState in RAM is ~770 bytes and does NOT go on the wire raw.
// This format carries only what a first-party DEVICE knows: its
// measurements, status, and alerts. Identity and user-config fields
// (device_id, source_id, group, name) are DISPLAY-owned and are NOT
// transmitted - the receiving display reconstructs device_id from the
// sender MAC + device_type + instance, and fills name/group/source_id
// from its local pairing record, transport/last_update_ms/rssi from
// the ESP-NOW receive metadata.
//
// All PrintedReef MCUs (ESP32-S3, ESP32-P4) are little-endian; this
// format is little-endian and performs no byte-swapping.
//
// On-wire packet layout:
//   [PrWireHeader]                              12 bytes
//   [PrWireValue  primary]                      38 bytes
//   [PrWireValue  secondary] x secondary_count  38 bytes each
//   [PrWireAlert]            x alert_count       13 bytes each
//   [uint16_t     crc16]                         2 bytes  (CRC-16/CCITT
//                                                over all preceding bytes)

#define PR_WIRE_VERSION         1
#define PR_ESPNOW_MAX_PAYLOAD   250    // ESP-NOW hard per-packet limit
#define PR_WIRE_MAGIC_0         0x50   // 'P'
#define PR_WIRE_MAGIC_1         0x52   // 'R'

// Wire limits (intentionally tighter than schema.h, to fit 250 bytes).
// A device needing more secondary values should split into sub-devices
// via the `instance` field.
#define PR_WIRE_MAX_SECONDARY   4
#define PR_WIRE_MAX_ALERTS      3

// Wire string buffers (shorter than schema.h; copies are bounded and
// truncating - keep device labels concise).
#define PR_WIRE_KEY_LEN        12
#define PR_WIRE_LABEL_LEN      14
#define PR_WIRE_UNIT_LEN        6
#define PR_WIRE_CODE_LEN       12

enum PrPacketType : uint8_t {
  PR_PKT_TELEMETRY = 1,   // a DeviceState report  (fully defined here)
  PR_PKT_PAIRING   = 2,   // pairing handshake     (reserved - TBD)
  PR_PKT_HEARTBEAT = 3,   // liveness ping         (reserved - TBD)
  PR_PKT_ACK       = 4,   // acknowledgement       (reserved - TBD)
};

enum PrWireResult : uint8_t {
  PR_WIRE_OK = 0,
  PR_WIRE_ERR_LENGTH,         // buffer too short / wrong total length
  PR_WIRE_ERR_MAGIC,          // not a PrintedReef packet
  PR_WIRE_ERR_WIRE_VERSION,   // wire protocol version unsupported
  PR_WIRE_ERR_SCHEMA_VERSION, // schema version newer than we support
  PR_WIRE_ERR_TYPE,           // not a packet type this function decodes
  PR_WIRE_ERR_COUNT,          // secondary/alert count out of range
  PR_WIRE_ERR_CRC,            // checksum mismatch
};

// ---- packed wire structs (exact on-wire byte layout) ----

typedef struct __attribute__((packed)) {
  uint8_t magic0;            // PR_WIRE_MAGIC_0
  uint8_t magic1;            // PR_WIRE_MAGIC_1
  uint8_t wire_version;      // PR_WIRE_VERSION
  uint8_t schema_version;    // PR_SCHEMA_VERSION
  uint8_t packet_type;       // PrPacketType
  uint8_t device_type;       // DeviceType
  uint8_t instance;          // sub-device index; 0 = primary / only
  uint8_t status;            // DeviceStatus reported by the device
  uint8_t seq;               // sequence number, wraps 0-255
  uint8_t secondary_count;   // WireValue entries after the primary
  uint8_t alert_count;       // WireAlert entries
  uint8_t reserved;          // pad to 12 bytes; send 0
} PrWireHeader;

typedef struct __attribute__((packed)) {
  char    key[PR_WIRE_KEY_LEN];
  char    label[PR_WIRE_LABEL_LEN];
  float   value;
  char    unit[PR_WIRE_UNIT_LEN];
  uint8_t decimals;
  uint8_t state;             // ValueState
} PrWireValue;

typedef struct __attribute__((packed)) {
  uint8_t severity;          // AlertSeverity
  char    code[PR_WIRE_CODE_LEN];
} PrWireAlert;

#define PR_WIRE_MAX_PACKET                                              \
  (sizeof(PrWireHeader) + (1 + PR_WIRE_MAX_SECONDARY) * sizeof(PrWireValue) \
   + PR_WIRE_MAX_ALERTS * sizeof(PrWireAlert) + 2)

static_assert(sizeof(PrWireHeader) == 12, "PrWireHeader must be 12 bytes");
static_assert(sizeof(PrWireValue)  == 38, "PrWireValue must be 38 bytes");
static_assert(sizeof(PrWireAlert)  == 13, "PrWireAlert must be 13 bytes");
static_assert(PR_WIRE_MAX_PACKET <= PR_ESPNOW_MAX_PAYLOAD,
              "PR wire format exceeds the 250-byte ESP-NOW limit");

// ---- helpers ----

// Bounded, always-null-terminating copy; zero-fills the remainder so the
// wire bytes are deterministic (stable CRC, no leaked stack contents).
static inline void pr_wire_strlcpy(char* dst, const char* src, size_t dstsz) {
  if (dstsz == 0) return;
  size_t i = 0;
  for (; i + 1 < dstsz && src[i]; ++i) dst[i] = src[i];
  for (; i < dstsz; ++i) dst[i] = '\0';
}

// CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF).
static inline uint16_t pr_wire_crc16(const uint8_t* data, size_t len) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < len; ++i) {
    crc ^= (uint16_t)data[i] << 8;
    for (int b = 0; b < 8; ++b)
      crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021)
                           : (uint16_t)(crc << 1);
  }
  return crc;
}

static inline void pr_wire_pack_value(const ValueEntry* v, PrWireValue* wv) {
  pr_wire_strlcpy(wv->key,   v->key,   PR_WIRE_KEY_LEN);
  pr_wire_strlcpy(wv->label, v->label, PR_WIRE_LABEL_LEN);
  wv->value    = v->value;
  pr_wire_strlcpy(wv->unit,  v->unit,  PR_WIRE_UNIT_LEN);
  wv->decimals = v->decimals;
  wv->state    = (uint8_t)v->state;
}

static inline void pr_wire_unpack_value(const PrWireValue* wv, ValueEntry* v) {
  pr_wire_strlcpy(v->key,   wv->key,   PR_KEY_LEN);
  pr_wire_strlcpy(v->label, wv->label, PR_LABEL_LEN);
  v->value    = wv->value;
  pr_wire_strlcpy(v->unit,  wv->unit,  PR_UNIT_LEN);
  v->decimals = wv->decimals;
  v->state    = (ValueState)wv->state;
  v->valid    = true;
}

// ---- encode (device side) ----

// Encode `st` into `buf` as a PR_PKT_TELEMETRY packet. Reads only the
// device-sourced fields: device_type, status, primary, secondary[],
// secondary_count, alerts[], alert_count.
// Returns the packet length in bytes, or -1 on error (too many secondary
// values or alerts for the wire limits, or buffer too small).
static inline int pr_wire_encode_telemetry(const DeviceState* st,
                                           uint8_t seq, uint8_t instance,
                                           uint8_t* buf, size_t bufsz) {
  if (!st || !buf) return -1;
  if (st->secondary_count > PR_WIRE_MAX_SECONDARY) return -1;
  if (st->alert_count     > PR_WIRE_MAX_ALERTS)    return -1;

  const uint8_t sec = st->secondary_count;
  const uint8_t alr = st->alert_count;
  const size_t  need = sizeof(PrWireHeader)
                     + (size_t)(1 + sec) * sizeof(PrWireValue)
                     + (size_t)alr * sizeof(PrWireAlert) + 2;
  if (bufsz < need || need > PR_ESPNOW_MAX_PAYLOAD) return -1;

  size_t off = 0;

  PrWireHeader h;
  h.magic0 = PR_WIRE_MAGIC_0;
  h.magic1 = PR_WIRE_MAGIC_1;
  h.wire_version    = PR_WIRE_VERSION;
  h.schema_version  = PR_SCHEMA_VERSION;
  h.packet_type     = PR_PKT_TELEMETRY;
  h.device_type     = (uint8_t)st->device_type;
  h.instance        = instance;
  h.status          = (uint8_t)st->status;
  h.seq             = seq;
  h.secondary_count = sec;
  h.alert_count     = alr;
  h.reserved        = 0;
  memcpy(buf + off, &h, sizeof(h)); off += sizeof(h);

  PrWireValue wv;
  pr_wire_pack_value(&st->primary, &wv);
  memcpy(buf + off, &wv, sizeof(wv)); off += sizeof(wv);
  for (uint8_t i = 0; i < sec; ++i) {
    pr_wire_pack_value(&st->secondary[i], &wv);
    memcpy(buf + off, &wv, sizeof(wv)); off += sizeof(wv);
  }

  for (uint8_t i = 0; i < alr; ++i) {
    PrWireAlert wa;
    wa.severity = (uint8_t)st->alerts[i].severity;
    pr_wire_strlcpy(wa.code, st->alerts[i].code, PR_WIRE_CODE_LEN);
    memcpy(buf + off, &wa, sizeof(wa)); off += sizeof(wa);
  }

  uint16_t crc = pr_wire_crc16(buf, off);
  memcpy(buf + off, &crc, sizeof(crc)); off += sizeof(crc);

  return (int)off;
}

// ---- decode (display side) ----

// Peek the packet type after validating magic only. Returns 0 if `buf`
// is too short or not a PrintedReef packet; otherwise the PrPacketType.
static inline uint8_t pr_wire_packet_type(const uint8_t* buf, size_t len) {
  if (!buf || len < sizeof(PrWireHeader)) return 0;
  if (buf[0] != PR_WIRE_MAGIC_0 || buf[1] != PR_WIRE_MAGIC_1) return 0;
  return buf[4];
}

// Decode a PR_PKT_TELEMETRY packet into `out`. Fills ONLY the device-
// sourced fields of DeviceState (schema_version, device_type, status,
// primary, secondary[], secondary_count, alerts[], alert_count; alert
// .message is cleared and .since_ms zeroed - the display derives the
// message from .code and timestamps the alert on first sight).
//
// The caller must fill device_id, source_id, group, name, transport,
// last_update_ms, and rssi from the pairing record and ESP-NOW receive
// metadata. `out_instance` (optional) receives the sub-device index so
// the caller can build the correct device_id.
static inline PrWireResult pr_wire_decode_telemetry(const uint8_t* buf,
                                                    size_t len,
                                                    DeviceState* out,
                                                    uint8_t* out_instance) {
  if (!buf || !out || len < sizeof(PrWireHeader) + 2) return PR_WIRE_ERR_LENGTH;

  PrWireHeader h;
  memcpy(&h, buf, sizeof(h));

  if (h.magic0 != PR_WIRE_MAGIC_0 || h.magic1 != PR_WIRE_MAGIC_1)
    return PR_WIRE_ERR_MAGIC;
  if (h.wire_version != PR_WIRE_VERSION)
    return PR_WIRE_ERR_WIRE_VERSION;
  if (h.schema_version > PR_SCHEMA_VERSION)   // older is fine; newer is not
    return PR_WIRE_ERR_SCHEMA_VERSION;
  if (h.packet_type != PR_PKT_TELEMETRY)
    return PR_WIRE_ERR_TYPE;
  if (h.secondary_count > PR_WIRE_MAX_SECONDARY ||
      h.alert_count     > PR_WIRE_MAX_ALERTS)
    return PR_WIRE_ERR_COUNT;

  const size_t need = sizeof(PrWireHeader)
                    + (size_t)(1 + h.secondary_count) * sizeof(PrWireValue)
                    + (size_t)h.alert_count * sizeof(PrWireAlert) + 2;
  if (len != need) return PR_WIRE_ERR_LENGTH;

  uint16_t rx_crc;
  memcpy(&rx_crc, buf + need - 2, sizeof(rx_crc));
  if (rx_crc != pr_wire_crc16(buf, need - 2)) return PR_WIRE_ERR_CRC;

  out->schema_version  = h.schema_version;
  out->device_type     = (DeviceType)h.device_type;
  out->status          = (DeviceStatus)h.status;
  out->secondary_count = h.secondary_count;
  out->alert_count     = h.alert_count;
  if (out_instance) *out_instance = h.instance;

  size_t off = sizeof(PrWireHeader);

  PrWireValue wv;
  memcpy(&wv, buf + off, sizeof(wv)); off += sizeof(wv);
  pr_wire_unpack_value(&wv, &out->primary);
  for (uint8_t i = 0; i < h.secondary_count; ++i) {
    memcpy(&wv, buf + off, sizeof(wv)); off += sizeof(wv);
    pr_wire_unpack_value(&wv, &out->secondary[i]);
  }

  for (uint8_t i = 0; i < h.alert_count; ++i) {
    PrWireAlert wa;
    memcpy(&wa, buf + off, sizeof(wa)); off += sizeof(wa);
    out->alerts[i].severity   = (AlertSeverity)wa.severity;
    pr_wire_strlcpy(out->alerts[i].code, wa.code, PR_CODE_LEN);
    out->alerts[i].message[0] = '\0';   // display derives message from code
    out->alerts[i].since_ms   = 0;      // display timestamps on first sight
  }

  return PR_WIRE_OK;
}

#endif  // PR_ESPNOW_WIRE_H

/* pr_cloud.h — AquaSensei cloud telemetry uplink (MQTT/TLS → AWS IoT Core).
 *
 * Purely ADDITIVE: publishes the telemetry the device already parses (Apex tank
 * probes, Trident, outlets, feed) to the AquaSensei platform over mutual-TLS
 * MQTT on its own FreeRTOS task. Never blocks boot / LVGL / the Apex poll. When
 * unconfigured (no endpoint) or offline it idles cheaply; the device stays fully
 * functional. v1 = publish-only telemetry + subscribe `cmd` (LOG ONLY, no
 * actuation). Demo/sim sources are NEVER published as real.
 *
 * Contract: src/shared/contracts/telemetry.v1.schema.json ("as.telemetry.v1").
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>   /* size_t */

#ifdef __cplusplus
extern "C" {
#endif

/* Start the cloud task (idempotent). Call once at boot after pr_net_init().
   Idles until Wi-Fi is ONLINE, the clock is NTP-set, and an endpoint is set. */
void pr_cloud_start(void);

/* Persist cloud config to NVS ("pr_cloud") and apply immediately.
   endpoint  = AWS IoT ATS data endpoint, host only (no scheme/port).
   account   = telemetry topic {account} segment (policy-wildcarded; placeholder ok).
   client_id = MQTT clientId / AWS thing name; "" => derive "pr-<sta-mac>". */
void pr_cloud_save_config(const char *endpoint, const char *account, const char *client_id);

/* Live status for the System screen (Settings → About & Updates → CLOUD). */
typedef struct {
    bool     connected;       /* MQTT session up                              */
    bool     connecting;      /* attempting / reconnecting                    */
    char     endpoint[96];    /* configured endpoint, "" if unset             */
    char     client_id[40];   /* "pr-<12hex>" (or thing-name override)        */
    int64_t  last_pub_us;     /* esp_timer time of last publish, 0 = never    */
    uint32_t pub_count;       /* successful publishes since boot              */
    int64_t  last_acct_us;    /* esp_timer time of last account.v1 RX, 0=never*/
    uint32_t acct_count;      /* account.v1 downlinks decoded since boot      */
    char     acct_detail[48]; /* last account outcome: tanks/score or error   */
    char     last_error[64];  /* last TLS/MQTT error, "" if none              */
    char     detail[48];      /* why idle: "Waiting for Wi-Fi" / "No internet
                                 (clock not set)" / "Not configured", "" if up */
} pr_cloud_status_t;

void pr_cloud_get_status(pr_cloud_status_t *out);

/* The device's cloud identity = "<account>/<client_id>" — i.e. the two middle
   segments of the MQTT telemetry topic `as/v1/<account>/<client_id>/telemetry`.
   This is the `cid` the AquaSensei pairing QR / claim flow keys off; it MUST be
   built from the same source as the topic (this getter is that source). Writes
   the cid to `out` and returns true; returns false if the client_id isn't
   resolvable yet (offline with no clientId override). */
bool pr_cloud_get_cid(char *out, size_t n);

/* AquaSensei account state, pushed back (retained) on
   as/v1/{account}/{display}/account with schema "as.account.v1": ONE payload per
   device carrying EVERY tank in the owner's account + a default_tank_id. This
   struct holds the device-PICKED tank (default or tanks[0]) plus the full tank
   list. DISPLAY STATE, never commands — the device renders it and acts on nothing. */
#define PR_CLOUD_STATE_MAX_PARAMS 8
#define PR_CLOUD_MAX_TANKS        8

typedef struct {
    char   key[16];      /* alk|calcium|magnesium|nitrate|phosphate|salinity|temp|ph */
    char   label[24];    /* human label, e.g. "Alkalinity"        */
    char   short_label[10]; /* tight-space abbreviation, e.g. "ALK"; "" if absent */
    char   unit[8];      /* e.g. "dKH"                            */
    char   status[10];   /* ok|low|high|off|tracking              */
    double value;        /* canonical unit                        */
    bool   has_value;
    double target_low, target_high;
    bool   has_targets;
    char     source[12];        /* raw slug: apex|hydros|manual|hanna|strip|
                                   refract|icp|computed; "" if absent       */
    uint32_t last_reading_at;   /* epoch s when taken at source; 0 if absent */
} pr_cloud_param_t;

typedef struct {
    bool   valid;                /* schema matched "as.account.v1"          */
    int    score;                /* 0..100, or -1 = unknown                 */
    char   band[12];             /* excellent|good|fair|poor|unknown        */
    char   headline[96];         /* verbatim, safe-to-display               */
    char   tank_id[24];
    char   tank_name[24];
    char   temp_unit[4];         /* display_units.temp: "F"/"C"/"" if absent */
    char   vol_unit[4];          /* display_units.volume: "gal"/"L"/"" — fwd-compat */
    char   device_type[16];      /* echoed device class slug; "" if absent  */
    char   tank_names[PR_CLOUD_MAX_TANKS][24];  /* every tank in the account (account.v1 tanks[]) */
    char   tank_ids[PR_CLOUD_MAX_TANKS][24];    /* matching ids, same index as tank_names */
    int    ntanks;
    char   default_tank_id[24];  /* account.v1 "show this first" preference; "" if null */
    int    nparams;
    pr_cloud_param_t params[PR_CLOUD_STATE_MAX_PARAMS];
    bool   show_linked;          /* fire the one-time "Linked" confirmation  */
    bool   tank_changed;         /* tank.id differs from the last seen       */
} pr_cloud_state_t;

/* Fired when a valid as.account.v1 arrives. Invoked on the MQTT client task —
   the handler MUST marshal any LVGL work via the display lock. The pointer is
   valid only for the duration of the call; copy anything you keep. */
typedef void (*pr_cloud_state_cb_t)(const pr_cloud_state_t *st);
void pr_cloud_set_state_cb(pr_cloud_state_cb_t cb);

/* Re-render a specific tank (by name) from the last account.v1 — the home-screen
   aquarium picker calls this when the user switches tanks. The choice is sticky
   (later pushes keep rendering it). Returns false if no account has arrived yet,
   in which case the choice applies to the next push. */
bool pr_cloud_select_tank(const char *name);

/* ---- device-specific seams ----------------------------------------------- *
 * as_core is fully device-agnostic: it owns the MQTT/TLS transport, identity,
 * the telemetry ENVELOPE, account.v1 parsing, and the claim URL — and depends on
 * NO device-specific component (no Wi-Fi driver, no sensor registry). Each
 * device's shell supplies its specifics through the seams below, registered once
 * at boot before pr_cloud_start(). */

/* Telemetry sources. as_core builds the envelope (schema/client_id/device_type/
   ts/fw_version); the shell builds EVERY source. The shell reads whatever it has
   (a sensor registry, Apex outlets/feed, …) and appends cJSON source objects to
   `sources_array` (a cJSON*, passed as void* so this header carries no cJSON
   dependency). Return true if ≥1 LIVE source was added — false means as_core
   publishes nothing this tick (e.g. a display-only device with no sensors). */
typedef struct {
    bool (*build_sources)(void *sources_array);
    /* seconds since the device's data last refreshed (freshness gate); <0 = unknown */
    int  (*secs_since_sync)(void);
} pr_cloud_telemetry_provider_t;
void pr_cloud_set_telemetry_provider(const pr_cloud_telemetry_provider_t *p);

/* Network. as_core needs only two things from the device's Wi-Fi stack: whether
   it's online (gate before connecting) and the STA MAC (to derive the clientId
   "pr-<12hex>"). The shell wires these to its own Wi-Fi — as_core never links a
   Wi-Fi component, so a device with its own onboarding plugs straight in. */
typedef struct {
    bool        (*is_online)(void);   /* true once the device has connectivity     */
    const char *(*mac)(void);         /* STA MAC "aa:bb:cc:dd:ee:ff"; "" if unknown */
} pr_cloud_net_provider_t;
void pr_cloud_set_net_provider(const pr_cloud_net_provider_t *p);

/* The device class slug echoed in telemetry.device_type (e.g. a desktop display
   or a round-OLED variant). Set once at boot before pr_cloud_start(); as_core
   ships no device-specific default. */
void pr_cloud_set_device_type(const char *device_type);

/* Build the AquaSensei pairing/claim URL for the QR:
   https://aquasensei.app/app/claim?cid=<url-encoded cid> (the cid's single '/'
   is percent-encoded as %2F). Writes into out; returns false if the cid isn't
   resolvable yet (same condition as pr_cloud_get_cid). */
bool pr_cloud_build_claim_url(char *out, size_t n);

#ifdef __cplusplus
}
#endif

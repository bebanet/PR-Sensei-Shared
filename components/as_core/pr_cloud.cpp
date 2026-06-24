/* pr_cloud.cpp — AquaSensei telemetry uplink: mutual-TLS MQTT to AWS IoT Core.
 *
 * Own task; never blocks boot/LVGL/Apex. Builds the "as.telemetry.v1" payload
 * from the live registry (sim/demo sources excluded) and publishes on the Apex
 * poll cadence + a heartbeat. Subscribes the cmd topic and LOGS ONLY (v1: no
 * actuation). Degrades to idle when offline / unconfigured.
 */
#include "pr_cloud.h"

#include <string.h>
#include <time.h>
#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_attr.h"        /* EXT_RAM_BSS_ATTR — keep the big snapshot off the stack */
#include "esp_netif_sntp.h"
#include "nvs.h"
#include "mqtt_client.h"
#include "cJSON.h"


/* Compile-time defaults seed NVS on first boot. Real values live in the
   gitignored cloud_secrets.h (copy from cloud_secrets.h.example). */
#if defined(__has_include)
#  if __has_include("cloud_secrets.h")
#    include "cloud_secrets.h"
#  endif
#endif
#ifndef PR_CLOUD_ENDPOINT
#  define PR_CLOUD_ENDPOINT ""
#endif
#ifndef PR_CLOUD_ACCOUNT
#  define PR_CLOUD_ACCOUNT "dev"
#endif
#ifndef PR_CLOUD_CLIENTID
#  define PR_CLOUD_CLIENTID ""
#endif
#ifndef PR_CLOUD_PROVISION_TEMPLATE
#  define PR_CLOUD_PROVISION_TEMPLATE ""   /* set => Fleet Provisioning by Claim on first boot */
#endif

static const char *TAG = "pr_cloud";

/* Embedded AWS IoT credentials — gitignored PEMs, EMBED_TXTFILES (null-term). */
extern const uint8_t aws_device_cert_pem_start[] asm("_binary_aws_device_cert_pem_start");
extern const uint8_t aws_private_key_pem_start[] asm("_binary_aws_private_key_pem_start");
extern const uint8_t aws_root_ca_pem_start[]     asm("_binary_aws_root_ca_pem_start");

#define PR_CLOUD_PORT          8883
#define PR_CLOUD_HEARTBEAT_MS  (5 * 60 * 1000)   /* publish at least every 5 min */
#define PR_CLOUD_NVS_NS        "pr_cloud"
#define PR_CLOUD_RX_MAX        8192              /* inbound account.v1 cap (PSRAM); carries
                                                    ALL tanks so it needs more headroom than
                                                    a single state.v1 tank; drop > this */

static esp_mqtt_client_handle_t s_client = NULL;
static bool s_started     = false;
static bool s_time_ok     = false;
static bool s_sntp_started = false;

static char s_endpoint[96]  = "";
static char s_account[32]   = "dev";
static char s_client_id[40] = "";        /* "pr-<12hex>" or thing-name override */

/* Device-specific seams (set by the shell before pr_cloud_start) — keep as_core
   free of any per-device code + any device-component dependency. See
   pr_cloud_set_telemetry_provider / _net_provider / _device_type. */
static const pr_cloud_telemetry_provider_t *s_tp = NULL;
static const pr_cloud_net_provider_t       *s_np = NULL;
static char s_device_type[16] = "";      /* telemetry.device_type slug; shell-set */

/* --- Fleet Provisioning by Claim (gated on a non-empty PR_CLOUD_PROVISION_TEMPLATE) -
 * When a template is set AND no device cert is stored in NVS, the embedded cert/key
 * are treated as a CLAIM cert: on first connect the device asks AWS for its own
 * unique cert, registers it via the template, stores it in NVS, and reconnects with
 * it. No template (e.g. the View 7) => the embedded cert is used directly as the
 * device cert (unchanged behaviour). */
static char s_prov_template[64] = "";
static EXT_RAM_BSS_ATTR char s_dev_cert[2048];   /* provisioned device cert (PEM, NVS-backed)   */
static EXT_RAM_BSS_ATTR char s_dev_key[2048];    /* provisioned device key  (PEM, NVS-backed)   */
static EXT_RAM_BSS_ATTR char s_prov_token[2048]; /* certificateOwnershipToken from create-keys  */
static bool    s_have_dev    = false;   /* a provisioned device cert is loaded -> use it        */
static int     s_prov_phase  = 0;       /* 0 idle, 1 awaiting create-cert, 2 awaiting register  */
static bool    s_prov_done   = false;   /* success -> cloud_task reconnects with the device cert*/
static bool    s_prov_retry  = false;   /* failure -> cloud_task tears down + backs off         */
static int64_t s_prov_next_us = 0;      /* don't re-attempt provisioning before this time       */

/* status fields: written by the cloud/MQTT-event tasks, read by the UI push.
   plain (not volatile) — cross-task reads are benign status, and the
   vTaskDelay/function-call barriers force reloads where it matters. */
static bool     s_connected   = false;
static bool     s_connecting  = false;
static uint32_t s_pub_count   = 0;
static int64_t  s_last_pub_us = 0;
static uint32_t s_acct_count   = 0;          /* account.v1 downlinks decoded ok    */
static int64_t  s_last_acct_us = 0;          /* esp_timer time of last account RX  */
static char     s_acct_detail[48] = "";      /* last account outcome: tanks/score or error */
/* /account subscribe backoff — if the broker FIN-closes us right after the
   /account subscribe (e.g. the IoT policy doesn't authorize it), repeated retries
   would also kill telemetry on the same connection. After a few short-lived
   connects, DEFER the subscribe (keep the connection up for telemetry) and retry
   /account periodically so it self-heals once the policy is fixed. */
#define ACCT_SHORT_US    (3LL  * 1000000)    /* connect dying <3s after subscribe = rejected */
#define ACCT_BACKOFF_N   3                   /* this many short connects → back off          */
#define ACCT_RETRY_US    (120LL * 1000000)   /* retry /account this often while backed off   */
static int64_t  s_conn_us       = 0;         /* esp_timer at last /account subscribe attempt */
static bool     s_acct_subd     = false;     /* subscribed /account on the current connection */
static int      s_acct_fails    = 0;         /* consecutive short-lived connects after subscribe */
static bool     s_acct_backoff  = false;     /* deferring /account subscribe (telemetry-only) */
static int64_t  s_acct_retry_us = 0;         /* next time to retry /account while backed off  */
/* Multi-tank: keep the last account.v1 raw so a tank switch can re-render another
   tank's params without waiting for the next push. s_sel_tank is the user's pick
   (by name); "" = follow default_tank_id. */
static EXT_RAM_BSS_ATTR char s_acct_raw[PR_CLOUD_RX_MAX];
static int  s_acct_raw_len = 0;
static char s_sel_tank[24] = "";
static volatile bool s_reselect = false;   /* UI task requested a tank switch; cloud task applies it */
static char              s_last_error[64] = "";
static char              s_status[48] = "Starting...";   /* why the cloud is idle */
static pr_cloud_state_cb_t s_state_cb = NULL;

/* Inbound reassembly: esp-mqtt fragments any payload larger than the ~1 KB RX
   buffer, so stitch fragments via total_data_len/current_data_offset into one
   fixed PSRAM buffer (no per-message malloc); oversize is dropped before parse. */
static EXT_RAM_BSS_ATTR char s_rx[PR_CLOUD_RX_MAX];
static int  s_rx_total    = 0;     /* total_data_len of the in-flight message, 0 = idle */
static bool s_rx_oversize = false;
static int  s_rx_kind = 0;  /* in-flight topic: 0 other,1 account,2/3 create acc/rej,4/5 provision acc/rej */
static EXT_RAM_BSS_ATTR pr_cloud_state_t s_state;   /* filled per message, passed to the cb */

/* ---- config (NVS, seeded from gitignored cloud_secrets.h) ---------------- */
static void load_config(void) {
    nvs_handle_t h;
    if (nvs_open(PR_CLOUD_NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        size_t n;
        n = sizeof s_endpoint;  nvs_get_str(h, "endpoint", s_endpoint,  &n);
        n = sizeof s_account;   nvs_get_str(h, "account",  s_account,   &n);
        n = sizeof s_client_id; nvs_get_str(h, "clientid", s_client_id, &n);
        n = sizeof s_prov_template; nvs_get_str(h, "provtpl", s_prov_template, &n);
        nvs_close(h);
    }
    if (!s_endpoint[0]  && PR_CLOUD_ENDPOINT[0])  strncpy(s_endpoint,  PR_CLOUD_ENDPOINT,  sizeof s_endpoint  - 1);
    if (!s_account[0])                            strncpy(s_account,   PR_CLOUD_ACCOUNT[0] ? PR_CLOUD_ACCOUNT : "dev", sizeof s_account - 1);
    if (!s_client_id[0] && PR_CLOUD_CLIENTID[0])  strncpy(s_client_id, PR_CLOUD_CLIENTID, sizeof s_client_id - 1);
    if (!s_prov_template[0] && PR_CLOUD_PROVISION_TEMPLATE[0]) strncpy(s_prov_template, PR_CLOUD_PROVISION_TEMPLATE, sizeof s_prov_template - 1);
}

void pr_cloud_save_config(const char *endpoint, const char *account, const char *client_id) {
    nvs_handle_t h;
    if (nvs_open(PR_CLOUD_NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        if (endpoint)  nvs_set_str(h, "endpoint", endpoint);
        if (account)   nvs_set_str(h, "account",  account);
        if (client_id) nvs_set_str(h, "clientid", client_id);
        nvs_commit(h); nvs_close(h);
    }
    load_config();
}

/* Derive clientId: NVS/secrets override wins, else "pr-" + STA MAC (lowercase,
   colons stripped) → matches the contract pattern ^pr-[0-9a-f]{12}$. */
static void build_client_id(void) {
    if (s_client_id[0]) return;
    const char *mac = (s_np && s_np->mac) ? s_np->mac() : "";  /* shell's STA MAC */
    char hex[13]; int j = 0;
    for (const char *p = mac; *p && j < 12; ++p) {
        char c = *p;
        if (c == ':') continue;
        if (c >= 'A' && c <= 'F') c = (char)(c - 'A' + 'a');
        hex[j++] = c;
    }
    hex[j] = '\0';
    if (j == 12) snprintf(s_client_id, sizeof s_client_id, "pr-%s", hex);
}

/* Public: the cloud identity = "<account>/<client_id>" — the two middle topic
   segments. The pairing QR / claim flow encodes exactly this, so it is built
   from the SAME s_account/s_client_id that construct the MQTT topic (no drift).
   Resolves the clientId on demand. False if the display segment isn't known yet. */
bool pr_cloud_get_cid(char *out, size_t n) {
    if (!out || n == 0) return false;
    out[0] = '\0';
    if (!s_client_id[0]) build_client_id();
    if (!s_account[0] || !s_client_id[0]) return false;
    snprintf(out, n, "%s/%s", s_account, s_client_id);
    return true;
}

void pr_cloud_set_state_cb(pr_cloud_state_cb_t cb) { s_state_cb = cb; }

void pr_cloud_set_telemetry_provider(const pr_cloud_telemetry_provider_t *p) { s_tp = p; }

void pr_cloud_set_net_provider(const pr_cloud_net_provider_t *p) { s_np = p; }

void pr_cloud_set_device_type(const char *device_type) {
    snprintf(s_device_type, sizeof s_device_type, "%s", device_type ? device_type : "");
}

/* Pairing/claim URL = https://aquasensei.app/app/claim?cid=<url-encoded cid>.
   The cid's single '/' is percent-encoded as %2F (rest is plain ASCII). Built
   here so the wire identity (host + cid) lives only in as_core, never the shell. */
bool pr_cloud_build_claim_url(char *out, size_t n) {
    if (!out || n == 0) return false;
    out[0] = '\0';
    char cid[80];
    if (!pr_cloud_get_cid(cid, sizeof cid)) return false;
    char enc[96]; size_t j = 0;
    for (const char *p = cid; *p && j + 3 < sizeof enc; ++p) {
        if (*p == '/') { enc[j++] = '%'; enc[j++] = '2'; enc[j++] = 'F'; }
        else            enc[j++] = *p;
    }
    enc[j] = '\0';
    snprintf(out, n, "https://aquasensei.app/app/claim?cid=%s", enc);
    return true;
}

/* small typed NVS helpers (namespace PR_CLOUD_NVS_NS) for the pairing-ack gate */
static bool nvs_get_u32_(const char *key, uint32_t *out) {
    nvs_handle_t h; bool ok = false;
    if (nvs_open(PR_CLOUD_NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        ok = (nvs_get_u32(h, key, out) == ESP_OK);
        nvs_close(h);
    }
    return ok;
}
static void nvs_set_u32_(const char *key, uint32_t v) {
    nvs_handle_t h;
    if (nvs_open(PR_CLOUD_NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u32(h, key, v); nvs_commit(h); nvs_close(h);
    }
}
static bool nvs_get_str_(const char *key, char *out, size_t n) {
    nvs_handle_t h; bool ok = false;
    if (nvs_open(PR_CLOUD_NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        ok = (nvs_get_str(h, key, out, &n) == ESP_OK);
        nvs_close(h);
    }
    return ok;
}
static void nvs_set_str_(const char *key, const char *v) {
    nvs_handle_t h;
    if (nvs_open(PR_CLOUD_NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_str(h, key, v); nvs_commit(h); nvs_close(h);
    }
}

/* ---- SNTP — TLS cert-date validation needs a correct clock; ts needs epoch */
static void start_sntp(void) {
    if (s_sntp_started) return;
    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    if (esp_netif_sntp_init(&cfg) == ESP_OK) s_sntp_started = true;
}
static bool time_is_set(void) {
    if (s_time_ok) return true;
    if (time(NULL) > 1700000000) s_time_ok = true;   /* > 2023-11-14 */
    return s_time_ok;
}

/* ---- telemetry payload (contract: as.telemetry.v1) ----------------------- */

/* Build the telemetry JSON. as_core owns only the ENVELOPE (schema/client_id/
   device_type/ts/fw_version); the shell's provider builds EVERY source — it's the
   one that knows the device's sensors. Returns a cJSON-allocated string (free with
   cJSON_free) or NULL when there is no live data to send. */
static char *build_payload(void) {
    cJSON *sources = cJSON_CreateArray();
    bool any_live = (s_tp && s_tp->build_sources) ? s_tp->build_sources(sources) : false;
    if (!any_live) { cJSON_Delete(sources); return NULL; }  /* nothing live to send */

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "schema", "as.telemetry.v1");
    cJSON_AddStringToObject(root, "client_id", s_client_id);
    cJSON_AddStringToObject(root, "device_type", s_device_type);
    cJSON_AddNumberToObject(root, "ts", (double)time(NULL));
    const esp_app_desc_t *app = esp_app_get_description();
    cJSON_AddStringToObject(root, "fw_version", app ? app->version : "?");
    cJSON_AddItemToObject(root, "sources", sources);

    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return out;
}

/* ---- inbound: state.v1 (AquaSensei → device) ---------------------------- *
 * AquaSensei NEVER publishes a cmd topic — state.v1 is the only downlink, and
 * its linked_at field is the sole source of the pairing toast. No cmd subscribe,
 * no claimed-event handler. */
static bool topic_ends_with(const char *t, int tlen, const char *suffix) {
    int sl = (int)strlen(suffix);
    return t && tlen >= sl && memcmp(t + tlen - sl, suffix, sl) == 0;
}
static void json_str_cpy(const cJSON *o, const char *k, char *out, size_t n) {
    const cJSON *v = cJSON_GetObjectItem(o, k);
    if (cJSON_IsString(v) && v->valuestring) snprintf(out, n, "%s", v->valuestring);
    else if (n) out[0] = '\0';
}
static double json_num(const cJSON *o, const char *k, bool *has) {
    const cJSON *v = cJSON_GetObjectItem(o, k);
    if (cJSON_IsNumber(v)) { if (has) *has = true; return v->valuedouble; }
    if (has) *has = false;
    return 0.0;
}

/* Parse ONE tank object (the account.v1 per-tank shape == the old state.v1 minus
   the outer wrapper) into the picked-tank fields of *st: score, band, headline,
   display_units, params[]. */
static void parse_tank(const cJSON *tank, pr_cloud_state_t *st) {
    const cJSON *score = cJSON_GetObjectItem(tank, "score");
    if (cJSON_IsObject(score)) {
        const cJSON *sv = cJSON_GetObjectItem(score, "value");
        if (cJSON_IsNumber(sv) && sv->valuedouble >= 0 && sv->valuedouble <= 100)
            st->score = (int)(sv->valuedouble + 0.5);
        json_str_cpy(score, "band", st->band, sizeof st->band);
    }
    if (st->score < 0) snprintf(st->band, sizeof st->band, "unknown");
    else if (!st->band[0])
        snprintf(st->band, sizeof st->band, st->score >= 90 ? "excellent"
               : st->score >= 75 ? "good" : st->score >= 50 ? "fair" : "poor");

    json_str_cpy(tank, "headline", st->headline, sizeof st->headline);

    const cJSON *du = cJSON_GetObjectItem(tank, "display_units");
    if (cJSON_IsObject(du)) {
        json_str_cpy(du, "temp",   st->temp_unit, sizeof st->temp_unit);
        json_str_cpy(du, "volume", st->vol_unit,  sizeof st->vol_unit);
    }

    const cJSON *params = cJSON_GetObjectItem(tank, "params");
    int np = 0;
    if (cJSON_IsArray(params)) {
        const cJSON *p;
        cJSON_ArrayForEach(p, params) {
            if (np >= PR_CLOUD_STATE_MAX_PARAMS) break;
            if (!cJSON_IsObject(p)) continue;
            bool hasv = false;
            double v = json_num(p, "value", &hasv);
            if (!hasv) continue;                       /* skip a param with no numeric value */
            pr_cloud_param_t *pp = &st->params[np];
            json_str_cpy(p, "key",    pp->key,         sizeof pp->key);
            json_str_cpy(p, "label",  pp->label,       sizeof pp->label);
            json_str_cpy(p, "short",  pp->short_label, sizeof pp->short_label);
            json_str_cpy(p, "unit",   pp->unit,        sizeof pp->unit);
            json_str_cpy(p, "status", pp->status,      sizeof pp->status);
            pp->value = v; pp->has_value = true;
            bool hl = false, hh = false;
            pp->target_low  = json_num(p, "target_low",  &hl);
            pp->target_high = json_num(p, "target_high", &hh);
            pp->has_targets = hl && hh;
            json_str_cpy(p, "source", pp->source, sizeof pp->source);
            bool hr = false; double tr = json_num(p, "last_reading_at", &hr);
            pp->last_reading_at = (hr && tr >= 0) ? (uint32_t)tr : 0;
            ++np;
        }
    }
    st->nparams = np;
}

/* Parse one account.v1 (from buf) and render ONE tank: the user-selected tank
   (s_sel_tank, by name) if present, else default_tank_id (by id), else tanks[0].
   The full tank list is always mirrored. Returns true if a valid payload was
   applied. Defensive: parse / schema / field errors are logged + dropped. */
static bool apply_account(const char *buf, int len) {
    cJSON *j = cJSON_ParseWithLength(buf, len);
    if (!j) {
        ESP_LOGW(TAG, "account.v1 parse error (%d B), dropped", len);
        snprintf(s_acct_detail, sizeof s_acct_detail, "parse error (%d B)", len);
        return false;
    }

    const cJSON *schema = cJSON_GetObjectItem(j, "schema");
    if (!cJSON_IsString(schema) || strcmp(schema->valuestring, "as.account.v1") != 0) {
        ESP_LOGW(TAG, "account schema '%s' != as.account.v1, dropped",
                 cJSON_IsString(schema) ? schema->valuestring : "?");
        snprintf(s_acct_detail, sizeof s_acct_detail, "bad schema '%.16s'",
                 cJSON_IsString(schema) ? schema->valuestring : "?");
        cJSON_Delete(j);
        return false;
    }

    pr_cloud_state_t *st = &s_state;
    memset(st, 0, sizeof *st);
    st->valid = true;
    st->score = -1;

    json_str_cpy(j, "device_type",     st->device_type,     sizeof st->device_type);
    json_str_cpy(j, "default_tank_id", st->default_tank_id, sizeof st->default_tank_id);

    /* mirror the full tank list (ids + names) and pick the tank to render:
       selected name → default_tank_id → first. tanks=[] is valid → "No tanks yet". */
    const cJSON *tanks = cJSON_GetObjectItem(j, "tanks");
    const cJSON *pick_sel = NULL, *pick_def = NULL;
    if (cJSON_IsArray(tanks)) {
        const cJSON *t;
        cJSON_ArrayForEach(t, tanks) {
            if (!cJSON_IsObject(t)) continue;
            char id[24] = {0}, nm[24] = {0};
            json_str_cpy(t, "id",   id, sizeof id);
            json_str_cpy(t, "name", nm, sizeof nm);
            if (st->ntanks < PR_CLOUD_MAX_TANKS) {
                snprintf(st->tank_ids[st->ntanks],   sizeof st->tank_ids[0],   "%s", id);
                snprintf(st->tank_names[st->ntanks], sizeof st->tank_names[0], "%s", nm);
                st->ntanks++;
            }
            if (!pick_sel && s_sel_tank[0] && !strcmp(nm, s_sel_tank)) pick_sel = t;
            if (!pick_def && st->default_tank_id[0] && !strcmp(id, st->default_tank_id)) pick_def = t;
        }
    }
    const cJSON *picked = pick_sel ? pick_sel : pick_def;
    if (!picked && cJSON_IsArray(tanks) && cJSON_GetArraySize(tanks) > 0)
        picked = cJSON_GetArrayItem(tanks, 0);

    if (picked) {
        json_str_cpy(picked, "id",   st->tank_id,   sizeof st->tank_id);
        json_str_cpy(picked, "name", st->tank_name, sizeof st->tank_name);
        parse_tank(picked, st);
    } else {
        snprintf(st->headline, sizeof st->headline, "No tanks yet");   /* paired, no tanks */
    }

    /* pairing-ack gate — linked_at is present ONLY on the post-claim push. Fire
       the toast on a first-ever or newer value; skip on equal (retained replay /
       tank-switch re-parse) or older. Persist the latest. */
    const cJSON *la = cJSON_GetObjectItem(j, "linked_at");
    if (cJSON_IsNumber(la)) {
        uint32_t v = (uint32_t)la->valuedouble, stored = 0;
        bool found = nvs_get_u32_("linked_at", &stored);
        if (!found || v > stored) { st->show_linked = true; nvs_set_u32_("linked_at", v); }
    }
    if (st->tank_id[0]) {                               /* rendering a different tank? */
        char prev[24];
        bool found = nvs_get_str_("tank_id", prev, sizeof prev);
        st->tank_changed = found && strcmp(prev, st->tank_id) != 0;
        if (!found || st->tank_changed) nvs_set_str_("tank_id", st->tank_id);
    }

    cJSON_Delete(j);
    if (st->ntanks > 0)
        snprintf(s_acct_detail, sizeof s_acct_detail, "%d tank%s, %s '%s' score %d",
                 st->ntanks, st->ntanks == 1 ? "" : "s",
                 st->tank_name[0] ? st->tank_name : "?", st->band, st->score);
    else
        snprintf(s_acct_detail, sizeof s_acct_detail, "no tanks yet");
    ESP_LOGI(TAG, "account.v1: tanks=%d render='%s' score=%d band=%s params=%d linked=%d dtype=%s '%s'",
             st->ntanks, st->tank_name, st->score, st->band, st->nparams, st->show_linked,
             st->device_type[0] ? st->device_type : "-", st->headline);
    if (s_state_cb) s_state_cb(st);
    return true;
}

/* MQTT entry: a fresh retained/pushed account.v1. Render it + keep a copy so a
   later tank switch can re-render another tank locally. */
static void handle_account(const char *buf, int len) {
    if (!apply_account(buf, len)) return;
    s_acct_count++;
    s_last_acct_us = esp_timer_get_time();
    s_acct_fails = 0; s_acct_backoff = false;   /* the subscribe clearly works now */
    if (len <= (int)sizeof s_acct_raw - 1) {
        memcpy(s_acct_raw, buf, len);
        s_acct_raw[len] = '\0';
        s_acct_raw_len  = len;
    }
}

/* Select a tank by NAME (the home-screen picker calls this on a switch). Sticky:
   the choice also governs which tank later pushes render. The actual re-render is
   deferred to the cloud task (so apply_account/on_cloud_state only ever run on ONE
   task — no race on s_state/s_srv with a concurrent MQTT push). Switch latency is
   one cloud-task tick (≤2 s); the local repaint via glance_push is immediate.
   Returns true if an account has arrived (so the switch will take effect). */
bool pr_cloud_select_tank(const char *name) {
    snprintf(s_sel_tank, sizeof s_sel_tank, "%s", name ? name : "");
    s_reselect = true;
    return s_acct_raw_len > 0;
}

/* ---- MQTT ---------------------------------------------------------------- */

/* Subscribe to the device's /account downlink and arm the short-connection timer
   (so a FIN right after this is attributed to the subscribe). */
static void sub_account(void) {
    if (!s_client) return;
    char cid[80], topic[128];
    pr_cloud_get_cid(cid, sizeof cid);
    snprintf(topic, sizeof topic, "as/v1/%s/account", cid);
    esp_mqtt_client_subscribe(s_client, topic, 1);   /* QoS 1 */
    s_acct_subd = true;
    s_conn_us   = esp_timer_get_time();
    ESP_LOGI(TAG, "subscribed account: %s", topic);
}

/* ---- Fleet Provisioning by Claim ----------------------------------------- */
/* Bump to force a one-time re-provision across all units (e.g. after changing the
   Thing-naming scheme): a stored generation != this clears the saved device cert. */
#define PR_PROV_GEN 2
/* Load a previously provisioned device cert/key from NVS (stored as strings). */
static void prov_load_nvs(void) {
    uint32_t gen = 0;
    nvs_get_u32_("provgen", &gen);
    if (gen != PR_PROV_GEN) {           /* provisioning scheme changed -> drop old cert, re-provision once */
        nvs_handle_t h;
        if (nvs_open(PR_CLOUD_NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
            nvs_erase_key(h, "devcert");
            nvs_erase_key(h, "devkey");
            nvs_set_u32(h, "provgen", PR_PROV_GEN);
            nvs_commit(h); nvs_close(h);
        }
        ESP_LOGW(TAG, "provisioning generation %u != %u — cleared device cert, will re-provision",
                 (unsigned)gen, PR_PROV_GEN);
        return;                          /* s_have_dev stays false -> provision with the claim cert */
    }
    if (nvs_get_str_("devcert", s_dev_cert, sizeof s_dev_cert) &&
        nvs_get_str_("devkey",  s_dev_key,  sizeof s_dev_key)) {
        s_have_dev = true;
        ESP_LOGI(TAG, "device certificate loaded from NVS (%u B) — skip provisioning",
                 (unsigned)strlen(s_dev_cert));
    }
}
static void prov_fail(const char *why) {
    ESP_LOGE(TAG, "provisioning failed: %s", why);
    snprintf(s_acct_detail, sizeof s_acct_detail, "provision: %.32s", why);
    snprintf(s_status, sizeof s_status, "Provisioning failed");
    s_prov_phase = 0;
    s_prov_retry = true;       /* cloud_task tears down + backs off, then retries the claim */
}
/* Step 1: ask AWS IoT for a fresh keypair + certificate (CreateKeysAndCertificate). */
static void prov_begin(void) {
    esp_mqtt_client_subscribe(s_client, "$aws/certificates/create/json/accepted", 1);
    esp_mqtt_client_subscribe(s_client, "$aws/certificates/create/json/rejected", 1);
    esp_mqtt_client_publish(s_client, "$aws/certificates/create/json", "", 0, 1, 0);
    s_prov_phase = 1;
    snprintf(s_status, sizeof s_status, "Provisioning (1/2)");
    ESP_LOGI(TAG, "fleet provisioning: requesting a device certificate");
}
/* Step 2 (create accepted): stash the new cert/key/token, then RegisterThing. */
static void prov_handle_create(const char *buf, int len) {
    cJSON *j = cJSON_ParseWithLength(buf, len);
    if (!j) { prov_fail("create parse"); return; }
    const cJSON *pem = cJSON_GetObjectItem(j, "certificatePem");
    const cJSON *key = cJSON_GetObjectItem(j, "privateKey");
    const cJSON *tok = cJSON_GetObjectItem(j, "certificateOwnershipToken");
    if (!cJSON_IsString(pem) || !cJSON_IsString(key) || !cJSON_IsString(tok)) {
        cJSON_Delete(j); prov_fail("create fields"); return;
    }
    snprintf(s_dev_cert,   sizeof s_dev_cert,   "%s", pem->valuestring);
    snprintf(s_dev_key,    sizeof s_dev_key,    "%s", key->valuestring);
    snprintf(s_prov_token, sizeof s_prov_token, "%s", tok->valuestring);
    cJSON_Delete(j);

    char base[160], t[200];
    snprintf(base, sizeof base, "$aws/provisioning-templates/%s/provision/json", s_prov_template);
    snprintf(t, sizeof t, "%s/accepted", base); esp_mqtt_client_subscribe(s_client, t, 1);
    snprintf(t, sizeof t, "%s/rejected", base); esp_mqtt_client_subscribe(s_client, t, 1);

    /* Send the FULL clientId ("pr-<mac>") as SerialNumber so the template names the
       Thing identically to the clientId and the topic's {display} segment — the
       device policy scopes every topic to ${iot:Connection.Thing.ThingName}. */
    const char *serial = s_client_id;
    cJSON *p = cJSON_CreateObject();
    cJSON_AddStringToObject(p, "certificateOwnershipToken", s_prov_token);
    cJSON *params = cJSON_AddObjectToObject(p, "parameters");
    cJSON_AddStringToObject(params, "SerialNumber", serial);
    cJSON_AddStringToObject(params, "DeviceType",  s_device_type);
    char *body = cJSON_PrintUnformatted(p);
    cJSON_Delete(p);
    if (body) { esp_mqtt_client_publish(s_client, base, body, 0, 1, 0); cJSON_free(body); }
    s_prov_phase = 2;
    snprintf(s_status, sizeof s_status, "Provisioning (2/2)");
    ESP_LOGI(TAG, "fleet provisioning: registering thing %s (%s)", serial, s_device_type);
}
/* Step 3 (register accepted): persist the device cert/key + flag a reconnect. */
static void prov_handle_provisioned(void) {
    nvs_set_str_("devcert", s_dev_cert);
    nvs_set_str_("devkey",  s_dev_key);
    s_have_dev   = true;
    s_prov_phase = 0;
    s_prov_done  = true;       /* cloud_task: drop the claim link, reconnect as the device */
    snprintf(s_status, sizeof s_status, "Provisioned");
    ESP_LOGI(TAG, "fleet provisioning DONE — device cert stored, reconnecting as \"%s\"", s_client_id);
}

static void on_mqtt(void *args, esp_event_base_t base, int32_t id, void *data) {
    (void)args; (void)base;
    esp_mqtt_event_handle_t e = (esp_mqtt_event_handle_t)data;
    switch ((esp_mqtt_event_id_t)id) {
    case MQTT_EVENT_CONNECTED: {
        s_connected = true; s_connecting = false; s_last_error[0] = '\0';
        if (s_prov_template[0] && !s_have_dev) {   /* connected with the CLAIM cert -> self-provision */
            prov_begin();
            break;
        }
        /* account is the ONLY downlink (no cmd topic). Subscribe on EVERY connect:
           MQTT subscriptions don't survive a reconnect, and gating this on a prior
           telemetry publish meant a device with a server record but no live Apex
           data this session would never subscribe (no claim toast, no health).
           Subscribing before the first telemetry is harmless — the topic just has
           no retained message yet. */
        s_conn_us   = esp_timer_get_time();
        s_acct_subd = false;
        if (!s_acct_backoff) {
            sub_account();
        } else {                                   /* deferring to keep telemetry alive */
            ESP_LOGW(TAG, "MQTT CONNECTED; /account subscribe deferred (backoff) — telemetry only");
        }
        break;
    }
    case MQTT_EVENT_DISCONNECTED:
        s_connected = false; s_connecting = true;
        s_rx_total = 0; s_rx_oversize = false; s_rx_kind = 0;  /* drop any partial msg */
        s_prov_phase = 0;                                     /* a dropped link restarts provisioning */
        if (s_acct_subd) {   /* attribute a quick drop after subscribing to the subscribe */
            if (esp_timer_get_time() - s_conn_us < ACCT_SHORT_US) {
                if (++s_acct_fails >= ACCT_BACKOFF_N && !s_acct_backoff) {
                    s_acct_backoff  = true;
                    s_acct_retry_us = esp_timer_get_time() + ACCT_RETRY_US;
                    ESP_LOGW(TAG, "/account subscribe keeps closing the link (%dx) — backing off so "
                                  "telemetry survives; verify the IoT policy authorizes .../account",
                             s_acct_fails);
                    snprintf(s_acct_detail, sizeof s_acct_detail, "subscribe rejected - check IoT policy");
                }
            } else {
                s_acct_fails = 0;   /* connection lived long enough → subscribe was fine */
            }
            s_acct_subd = false;
        }
        ESP_LOGW(TAG, "MQTT disconnected");
        break;
    case MQTT_EVENT_DATA: {
        /* Reassemble fragments into s_rx, then hand a complete account.v1 to the
           decoder. Only the FIRST fragment (offset 0) carries the topic, so
           classify there. (account is the only topic we subscribe to.) */
        if (e->current_data_offset == 0) {
            const char *tp = e->topic; int tl = e->topic_len;
            s_rx_total    = e->total_data_len;
            s_rx_oversize = (e->total_data_len > (int)sizeof(s_rx) - 1);
            s_rx_kind = topic_ends_with(tp, tl, "/account")                            ? 1
                      : topic_ends_with(tp, tl, "/certificates/create/json/accepted")  ? 2
                      : topic_ends_with(tp, tl, "/certificates/create/json/rejected")  ? 3
                      : topic_ends_with(tp, tl, "/provision/json/accepted")            ? 4
                      : topic_ends_with(tp, tl, "/provision/json/rejected")            ? 5 : 0;
        }
        if (!s_rx_oversize && s_rx_total > 0 &&
            e->current_data_offset + e->data_len <= (int)sizeof(s_rx) - 1)
            memcpy(s_rx + e->current_data_offset, e->data, e->data_len);

        if (s_rx_total > 0 && e->current_data_offset + e->data_len >= s_rx_total) {
            if (s_rx_oversize) {
                ESP_LOGW(TAG, "inbound %d B > %d cap, dropped", s_rx_total, (int)sizeof(s_rx) - 1);
            } else {
                s_rx[s_rx_total] = '\0';
                switch (s_rx_kind) {
                case 1: handle_account(s_rx, s_rx_total); break;
                case 2: prov_handle_create(s_rx, s_rx_total); break;
                case 4: prov_handle_provisioned();           break;
                case 3: ESP_LOGE(TAG, "create-cert rejected: %.200s", s_rx); prov_fail("create rejected"); break;
                case 5: ESP_LOGE(TAG, "register rejected: %.200s",   s_rx); prov_fail("register rejected"); break;
                default: break;
                }
            }
            s_rx_total = 0; s_rx_oversize = false; s_rx_kind = 0;
        }
        break;
    }
    case MQTT_EVENT_ERROR:
        s_connected = false;
        if (e->error_handle) {
            if (e->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT)
                snprintf(s_last_error, sizeof s_last_error, "TLS err 0x%x (tls 0x%x)",
                         e->error_handle->esp_tls_last_esp_err,
                         e->error_handle->esp_tls_stack_err);
            else
                snprintf(s_last_error, sizeof s_last_error, "MQTT refused (rc %d)",
                         e->error_handle->connect_return_code);
        }
        ESP_LOGE(TAG, "MQTT error: %s", s_last_error);
        break;
    default: break;
    }
}

static void mqtt_connect(void) {
    if (s_client) return;
    esp_mqtt_client_config_t cfg = {};
    cfg.broker.address.hostname  = s_endpoint;
    cfg.broker.address.port      = PR_CLOUD_PORT;
    cfg.broker.address.transport = MQTT_TRANSPORT_OVER_SSL;     /* SNI on by default */
    cfg.broker.verification.certificate        = (const char *)aws_root_ca_pem_start;     /* server CA */
    cfg.credentials.client_id                  = s_client_id;
    /* A provisioned device cert in NVS wins; otherwise the embedded cert — which is
       the CLAIM cert when a provisioning template is set, or the device cert. */
    cfg.credentials.authentication.certificate = s_have_dev ? s_dev_cert : (const char *)aws_device_cert_pem_start;
    cfg.credentials.authentication.key         = s_have_dev ? s_dev_key  : (const char *)aws_private_key_pem_start;
    cfg.session.keepalive = 60;

    s_client = esp_mqtt_client_init(&cfg);
    if (!s_client) { snprintf(s_last_error, sizeof s_last_error, "mqtt init failed"); return; }
    esp_mqtt_client_register_event(s_client, MQTT_EVENT_ANY, on_mqtt, NULL);
    s_connecting = true;
    /* If start fails (e.g. "Error create mqtt task" under transient internal-RAM
       pressure), TEAR THE CLIENT DOWN and clear s_client so the cloud task retries
       on its next tick — otherwise s_client stays set-but-dead and the device is
       wedged offline until a reboot (the "went offline mid-session" failure). */
    esp_err_t err = esp_mqtt_client_start(s_client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mqtt start failed: %s — tearing down, will retry", esp_err_to_name(err));
        esp_mqtt_client_destroy(s_client);
        s_client = NULL;
        s_connecting = false;
        snprintf(s_last_error, sizeof s_last_error, "mqtt start failed (%s)", esp_err_to_name(err));
        return;
    }
    ESP_LOGI(TAG, "MQTT connecting to %s:%d as \"%s\"", s_endpoint, PR_CLOUD_PORT, s_client_id);
}

/* ---- task ---------------------------------------------------------------- */
static void *cjson_malloc(size_t s) { return heap_caps_malloc(s, MALLOC_CAP_SPIRAM); }
static void  cjson_free(void *p)    { heap_caps_free(p); }

static void cloud_task(void *arg) {
    (void)arg;
    cJSON_Hooks hooks = { cjson_malloc, cjson_free };
    cJSON_InitHooks(&hooks);

    int     prev_age   = 1 << 30;
    int64_t last_pub_ms = -PR_CLOUD_HEARTBEAT_MS;   /* allow an early first publish */
    bool    announced  = false;
    int     diag_tick  = 0;

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(2000));

        /* UI-task tank switch (pr_cloud_select_tank) deferred here so apply_account
           runs only on this task — no concurrent-write race on s_state/s_srv. */
        if (s_reselect && s_acct_raw_len > 0) {
            s_reselect = false;
            apply_account(s_acct_raw, s_acct_raw_len);
        }

        /* Reliability watch: free INTERNAL heap is what the esp-mqtt task stack
           draws from (~6.5 KB) — log it every ~60 s so a soak can confirm the
           LVGL-stack-to-PSRAM move keeps a healthy margin and there's no leak. */
        if (++diag_tick % 30 == 0)
            ESP_LOGI(TAG, "diag: free internal heap = %u B (min %u)",
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                     (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL));

        if (!(s_np && s_np->is_online && s_np->is_online())) {
            snprintf(s_status, sizeof s_status, "Waiting for Wi-Fi");
            continue;
        }

        if (!s_client_id[0]) build_client_id();
        if (s_client_id[0] && !announced) {
            ESP_LOGW(TAG, "==== MQTT clientId = \"%s\" — MUST match the AWS IoT thing name ====",
                     s_client_id);
            announced = true;
        }

        if (!s_endpoint[0]) {                  /* not configured → idle (device unaffected) */
            snprintf(s_status, sizeof s_status, "Not configured");
            continue;
        }

        start_sntp();
        if (!time_is_set()) {                  /* TLS cert dates need a real clock */
            snprintf(s_status, sizeof s_status, "No clock — check internet");
            continue;
        }

        /* fleet provisioning lifecycle: drop the claim link + reconnect as the
           device on success; tear down + back off 30 s on failure. */
        if (s_prov_done)  { s_prov_done = false;
            if (s_client) { esp_mqtt_client_destroy(s_client); s_client = NULL; }
            s_connected = false; s_connecting = false; }
        if (s_prov_retry) { s_prov_retry = false;
            if (s_client) { esp_mqtt_client_destroy(s_client); s_client = NULL; }
            s_connected = false; s_connecting = false;
            s_prov_next_us = esp_timer_get_time() + 30LL * 1000000; }
        if (s_prov_next_us) {
            if (esp_timer_get_time() < s_prov_next_us) {
                snprintf(s_status, sizeof s_status, "Provisioning retry soon"); continue;
            }
            s_prov_next_us = 0;
        }

        if (!s_client) mqtt_connect();
        if (!s_connected) { snprintf(s_status, sizeof s_status, "Connecting..."); continue; }
        if (s_prov_template[0] && !s_have_dev) {   /* claim connection: provisioning runs via MQTT events */
            snprintf(s_status, sizeof s_status, "Provisioning...");
            continue;
        }
        s_status[0] = '\0';                    /* connected — no idle reason */

        /* While backed off (telemetry-only), periodically re-try the /account
           subscribe on the live link so the device self-heals once the policy is
           fixed. A still-bad subscribe FIN-closes us → DISCONNECTED re-arms backoff. */
        if (s_acct_backoff && esp_timer_get_time() >= s_acct_retry_us) {
            s_acct_retry_us = esp_timer_get_time() + ACCT_RETRY_US;
            s_acct_backoff  = false;           /* clean attempt; re-backs-off on failure */
            ESP_LOGI(TAG, "retrying /account subscribe after backoff");
            sub_account();
        }

        int  age   = (s_tp && s_tp->secs_since_sync) ? s_tp->secs_since_sync() : -1;
        bool fresh = (age >= 0 && age < prev_age);
        if (age >= 0) prev_age = age;
        int64_t now_ms   = esp_timer_get_time() / 1000;
        bool heartbeat   = (now_ms - last_pub_ms >= PR_CLOUD_HEARTBEAT_MS);

        if (fresh || heartbeat) {
            char *payload = build_payload();
            if (payload) {
                char cid[80], topic[128];
                pr_cloud_get_cid(cid, sizeof cid);
                snprintf(topic, sizeof topic, "as/v1/%s/telemetry", cid);   /* same cid as the QR */
                int mid = esp_mqtt_client_publish(s_client, topic, payload, 0, 1, 0); /* QoS1, no retain */
                if (mid >= 0) {
                    s_pub_count++;
                    s_last_pub_us = esp_timer_get_time();
                    last_pub_ms = now_ms;
                    ESP_LOGI(TAG, "published telemetry %u B → %s [#%u]",
                             (unsigned)strlen(payload), topic, (unsigned)s_pub_count);
                }   /* state subscribe now happens unconditionally in MQTT_EVENT_CONNECTED */
                cJSON_free(payload);
            } else if (heartbeat) {
                last_pub_ms = now_ms;  /* no live data yet; don't spin the heartbeat */
            }
        }
    }
}

void pr_cloud_start(void) {
    if (s_started) return;
    s_started = true;
    load_config();
    prov_load_nvs();        /* if a device cert is already stored, skip provisioning */
    xTaskCreatePinnedToCore(cloud_task, "pr_cloud", 8192, NULL, 3, NULL, 0);
}

void pr_cloud_get_status(pr_cloud_status_t *out) {
    if (!out) return;
    memset(out, 0, sizeof *out);
    out->connected   = s_connected;
    out->connecting  = s_connecting;
    snprintf(out->endpoint,  sizeof out->endpoint,  "%s", s_endpoint);
    snprintf(out->client_id, sizeof out->client_id, "%s", s_client_id);
    out->last_pub_us  = s_last_pub_us;
    out->pub_count    = s_pub_count;
    out->last_acct_us = s_last_acct_us;
    out->acct_count   = s_acct_count;
    snprintf(out->acct_detail, sizeof out->acct_detail, "%s", s_acct_detail);
    snprintf(out->last_error, sizeof out->last_error, "%s", s_last_error);
    snprintf(out->detail,     sizeof out->detail,     "%s", s_status);
}

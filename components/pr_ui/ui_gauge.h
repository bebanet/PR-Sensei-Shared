/* ui_gauge.h — the shared PrintedReef arc card (pr_ui).
 *
 * ONE arc widget for the whole fleet. Defaults reproduce the 7" desktop look
 * (270° arc, gap at bottom, no pill, no secondaries). The round 1.75" AMOLED
 * configures the same widget for its full-perimeter hero: wider sweep, an
 * "IN RANGE" pill, and up to two secondaries with trend carets at the bottom
 * (e.g. Trident's Calcium / Magnesium under the Alkalinity hero).
 *
 *   - neutral-grey track over [arc_start, arc_start+arc_sweep]
 *   - a single status-coloured value DOT on the track (never a filled sweep)
 *   - two grey target ticks at the safe-range edges
 *   - centred name (above) + big value + unit + optional pill
 *   - optional 24 h sparkline + "TARGET lo–hi" caption
 *   - optional secondaries row
 *
 * Geometry: angle = arc_start + arc_sweep·frac, clockwise from east (LVGL's
 * native arc convention). Same math as design/glance-reefhealth gaugeSVG().
 */
#pragma once
#include "lvgl.h"
#include "pr_ui_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PR_GAUGE_MAX_SEC 2

typedef struct {
    const char *label;   /* "CALCIUM"          */
    const char *value;   /* "437"              */
    pr_trend_t  trend;   /* caret; NONE = none */
} pr_gauge_sec_t;

typedef struct {
    const char       *name;        /* "pH", "ALKALINITY" …                   */
    const char       *value;       /* preformatted, e.g. "8.3"               */
    const char       *unit;        /* "dKH", "°F", "" or NULL                */
    pr_trend_t        trend;       /* caret after the value; NONE = none     */
    const char       *target;      /* "TARGET 8.0–9.5" or NULL               */
    pr_status_t       status;      /* drives dot + value + pill colour       */

    float             frac;        /* value position on the sweep, 0..1      */
    float             lo_frac;     /* lower target tick, 0..1                */
    float             hi_frac;     /* upper target tick, 0..1                */
    const char       *min_label;   /* sweep-min end label (optional)         */
    const char       *max_label;   /* sweep-max end label (optional)         */

    const lv_coord_t *spark_pts;   /* 24 h trend, oldest→newest, or NULL     */
    int               spark_n;     /* point count (0 = no sparkline)         */

    const lv_font_t  *value_font;  /* big-number font; NULL → montserrat_48  */

    /* ---- per-face configuration (0 = use the 7" desktop defaults) -------- */
    float             arc_start;   /* sweep start angle, deg; 0 → 135        */
    float             arc_sweep;   /* sweep extent, deg;     0 → 270         */
    bool              show_pill;   /* IN RANGE / ATTENTION / ALERT pill      */
    pr_gauge_sec_t    sec[PR_GAUGE_MAX_SEC];  /* bottom secondaries          */
    int               sec_n;       /* 0 = none                               */
} pr_gauge_cfg_t;

/* Build one arc card under `parent`. Caller sizes it. */
lv_obj_t *pr_gauge_create(lv_obj_t *parent, const pr_gauge_cfg_t *cfg);

#ifdef __cplusplus
}
#endif

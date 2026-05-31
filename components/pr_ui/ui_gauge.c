/* ui_gauge.c — shared PrintedReef arc card (pr_ui, LVGL 9.5).
 *
 * The arc, ticks and value dot are rendered in a custom LV_EVENT_DRAW_MAIN
 * callback (lv_draw_arc / lv_draw_line / lv_draw_rect) so the geometry is
 * exact. Text + sparkline are child widgets. One widget serves every face;
 * see ui_gauge.h for the per-face config (sweep / pill / secondaries).
 */
#include "ui_gauge.h"
#include "ui_theme.h"

#include <math.h>
#include <stdlib.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define GAUGE_TICK_COLOR  lv_color_hex(0xC2C8CF)
#define GAUGE_DEF_START   135.0f
#define GAUGE_DEF_SWEEP   270.0f

typedef struct {
    lv_color_t dot_color;
    float      frac, lo_frac, hi_frac;
    float      arc_start, arc_sweep;
} gauge_draw_t;

static lv_color_t status_color(pr_status_t s)
{
    return s == PR_WARN ? PR_AMBER : s == PR_ALERT ? PR_RED : PR_TEAL;
}

static void polar(int32_t cx, int32_t cy, float r, float deg,
                  int32_t *ox, int32_t *oy)
{
    float a = deg * (float)M_PI / 180.0f;
    *ox = cx + (int32_t)lroundf(r * cosf(a));
    *oy = cy + (int32_t)lroundf(r * sinf(a));
}

static void gauge_delete_cb(lv_event_t *e)
{
    gauge_draw_t *st = (gauge_draw_t *)lv_event_get_user_data(e);
    if (st) lv_free(st);
}

static void gauge_draw_cb(lv_event_t *e)
{
    lv_obj_t     *obj   = (lv_obj_t *)lv_event_get_target(e);
    gauge_draw_t *st    = (gauge_draw_t *)lv_event_get_user_data(e);
    lv_layer_t   *layer = lv_event_get_layer(e);
    if (!st || !layer) return;

    lv_area_t c;
    lv_obj_get_coords(obj, &c);
    int32_t w  = lv_area_get_width(&c);
    int32_t h  = lv_area_get_height(&c);
    int32_t cx = (c.x1 + c.x2) / 2;
    int32_t cy = (c.y1 + c.y2) / 2;
    int32_t dim = w < h ? w : h;

    int32_t sw = dim / 16;
    if (sw < 5)  sw = 5;
    if (sw > 12) sw = 12;
    float   R  = dim / 2.0f - (sw / 2.0f) - 12.0f;
    if (R < 10) R = 10;

    const float A0 = st->arc_start, SPAN = st->arc_sweep;

    /* track */
    lv_draw_arc_dsc_t adsc;
    lv_draw_arc_dsc_init(&adsc);
    adsc.color       = PR_LINE;
    adsc.width       = sw;
    adsc.center.x    = cx;
    adsc.center.y    = cy;
    adsc.radius      = (int32_t)lroundf(R);
    adsc.start_angle = (int32_t)A0;
    adsc.end_angle   = (int32_t)(A0 + SPAN);
    adsc.rounded     = 1;
    lv_draw_arc(layer, &adsc);

    /* target ticks */
    lv_draw_line_dsc_t ldsc;
    lv_draw_line_dsc_init(&ldsc);
    ldsc.color       = GAUGE_TICK_COLOR;
    ldsc.width       = 3;
    ldsc.round_start = 1;
    ldsc.round_end   = 1;
    float r_in  = R + sw / 2.0f + 2.0f;
    float r_out = R + sw / 2.0f + 8.0f;
    for (int i = 0; i < 2; ++i) {
        float frac = i == 0 ? st->lo_frac : st->hi_frac;
        float ang  = A0 + SPAN * frac;
        int32_t ix, iy, ox, oy;
        polar(cx, cy, r_in,  ang, &ix, &iy);
        polar(cx, cy, r_out, ang, &ox, &oy);
        ldsc.p1.x = ix; ldsc.p1.y = iy;
        ldsc.p2.x = ox; ldsc.p2.y = oy;
        lv_draw_line(layer, &ldsc);
    }

    /* value dot */
    float ang = A0 + SPAN * st->frac;
    int32_t dx, dy;
    polar(cx, cy, R, ang, &dx, &dy);
    float dotR = sw * 0.78f;

    lv_draw_rect_dsc_t halo;
    lv_draw_rect_dsc_init(&halo);
    halo.radius   = LV_RADIUS_CIRCLE;
    halo.bg_color = st->dot_color;
    halo.bg_opa   = LV_OPA_20;
    int32_t hr = (int32_t)lroundf(dotR + 4);
    lv_area_t ha = { dx - hr, dy - hr, dx + hr, dy + hr };
    lv_draw_rect(layer, &halo, &ha);

    lv_draw_rect_dsc_t dot;
    lv_draw_rect_dsc_init(&dot);
    dot.radius       = LV_RADIUS_CIRCLE;
    dot.bg_color     = st->dot_color;
    dot.bg_opa       = LV_OPA_COVER;
    dot.border_color = PR_BG;
    dot.border_width = 2;
    int32_t dr = (int32_t)lroundf(dotR);
    lv_area_t da = { dx - dr, dy - dr, dx + dr, dy + dr };
    lv_draw_rect(layer, &dot, &da);
}

/* ---- helpers ------------------------------------------------------------ */

static lv_obj_t *plain(lv_obj_t *p)
{
    lv_obj_t *o = lv_obj_create(p);
    lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(o, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    return o;
}

static lv_obj_t *label(lv_obj_t *p, const char *txt,
                       const lv_font_t *font, lv_color_t col)
{
    lv_obj_t *l = lv_label_create(p);
    lv_label_set_text(l, txt ? txt : "");
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, col, 0);
    return l;
}

static lv_obj_t *build_spark(lv_obj_t *parent, const pr_gauge_cfg_t *cfg)
{
    lv_obj_t *chart = lv_chart_create(parent);
    lv_obj_set_width(chart, lv_pct(88));
    lv_obj_set_height(chart, 26);
    lv_obj_set_style_bg_opa(chart, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(chart, 0, 0);
    lv_obj_set_style_pad_all(chart, 0, 0);
    lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
    lv_chart_set_div_line_count(chart, 0, 0);
    lv_chart_set_point_count(chart, cfg->spark_n);
    lv_obj_set_style_width(chart,  0, LV_PART_INDICATOR);
    lv_obj_set_style_height(chart, 0, LV_PART_INDICATOR);
    lv_obj_set_style_line_width(chart, 2, LV_PART_ITEMS);

    lv_coord_t lo = cfg->spark_pts[0], hi = cfg->spark_pts[0];
    for (int i = 1; i < cfg->spark_n; ++i) {
        if (cfg->spark_pts[i] < lo) lo = cfg->spark_pts[i];
        if (cfg->spark_pts[i] > hi) hi = cfg->spark_pts[i];
    }
    lv_coord_t pad = (hi - lo) / 6;
    if (pad < 1) pad = 1;
    lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, lo - pad, hi + pad);

    lv_chart_series_t *s =
        lv_chart_add_series(chart, status_color(cfg->status),
                            LV_CHART_AXIS_PRIMARY_Y);
    for (int i = 0; i < cfg->spark_n; ++i)
        lv_chart_set_value_by_id(chart, s, i, cfg->spark_pts[i]);
    lv_chart_refresh(chart);
    return chart;
}

static lv_obj_t *caret(lv_obj_t *p, pr_trend_t t, lv_color_t col)
{
    if (t == PR_TREND_NONE) return NULL;
    lv_obj_t *c = label(p, t == PR_UP ? LV_SYMBOL_UP : LV_SYMBOL_DOWN,
                        &lv_font_montserrat_12, col);
    return c;
}

/* ---- the card ----------------------------------------------------------- */

lv_obj_t *pr_gauge_create(lv_obj_t *parent, const pr_gauge_cfg_t *cfg)
{
    lv_color_t col = status_color(cfg->status);

    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_remove_style_all(card);
    lv_obj_add_style(card, &pr_st_card, 0);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(card, 0, 0);
    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
    if (cfg->status == PR_WARN)
        lv_obj_set_style_border_color(card, lv_color_hex(0x6E5A2E), 0);
    else if (cfg->status == PR_ALERT)
        lv_obj_set_style_border_color(card, lv_color_hex(0x7A3A33), 0);

    /* NAME header — top */
    lv_obj_t *name = label(card, cfg->name, &lv_font_montserrat_16,
                           cfg->status == PR_OK ? PR_GREY : col);
    lv_obj_set_width(name, lv_pct(100));
    lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(name, LV_ALIGN_TOP_MID, 0, 8);

    /* gauge area — draws the arc */
    lv_obj_t *gw = lv_obj_create(card);
    lv_obj_remove_style_all(gw);
    lv_obj_remove_flag(gw, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(gw, lv_pct(100), lv_pct(62));
    lv_obj_align(gw, LV_ALIGN_CENTER, 0, 6);

    gauge_draw_t *st = (gauge_draw_t *)lv_malloc(sizeof(gauge_draw_t));
    st->dot_color = col;
    st->frac    = cfg->frac    < 0 ? 0 : (cfg->frac    > 1 ? 1 : cfg->frac);
    st->lo_frac = cfg->lo_frac < 0 ? 0 : (cfg->lo_frac > 1 ? 1 : cfg->lo_frac);
    st->hi_frac = cfg->hi_frac < 0 ? 0 : (cfg->hi_frac > 1 ? 1 : cfg->hi_frac);
    /* per-face sweep; 0 → desktop defaults (270° gap at bottom) */
    if (cfg->arc_sweep > 0.0f) {
        st->arc_start = cfg->arc_start;
        st->arc_sweep = cfg->arc_sweep;
    } else {
        st->arc_start = GAUGE_DEF_START;
        st->arc_sweep = GAUGE_DEF_SWEEP;
    }
    lv_obj_add_event_cb(gw, gauge_draw_cb,   LV_EVENT_DRAW_MAIN, st);
    lv_obj_add_event_cb(gw, gauge_delete_cb, LV_EVENT_DELETE,    st);

    /* centred value (+ unit, + optional pill) */
    lv_obj_t *vcol = plain(gw);
    lv_obj_set_flex_flow(vcol, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(vcol, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_align(vcol, LV_ALIGN_CENTER, 0, 0);
    const lv_font_t *vfont = cfg->value_font ? cfg->value_font
                                             : &lv_font_montserrat_48;
    label(vcol, cfg->value, vfont, col);
    if (cfg->unit && cfg->unit[0]) {
        lv_obj_t *u = label(vcol, cfg->unit, &lv_font_montserrat_14, PR_GREY);
        lv_obj_set_style_pad_top(u, 1, 0);
    }
    if (cfg->show_pill) {
        const char *state = cfg->status == PR_WARN  ? "ATTENTION"
                          : cfg->status == PR_ALERT ? "ALERT" : "IN RANGE";
        lv_obj_t *pill = plain(vcol);
        lv_obj_set_style_bg_color(pill, col, 0);
        lv_obj_set_style_bg_opa(pill, LV_OPA_20, 0);
        lv_obj_set_style_radius(pill, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_pad_hor(pill, 12, 0);
        lv_obj_set_style_pad_ver(pill, 3, 0);
        lv_obj_set_style_margin_top(pill, 7, 0);
        label(pill, state, &lv_font_montserrat_12, col);
    }

    /* foot — secondaries (round style) OR target caption (desktop) */
    if (cfg->sec_n > 0) {
        lv_obj_t *srow = plain(card);
        lv_obj_set_width(srow, lv_pct(96));
        lv_obj_set_flex_flow(srow, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(srow, LV_FLEX_ALIGN_SPACE_EVENLY,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_align(srow, LV_ALIGN_BOTTOM_MID, 0, -2);
        for (int i = 0; i < cfg->sec_n && i < PR_GAUGE_MAX_SEC; ++i) {
            lv_obj_t *cell = plain(srow);
            lv_obj_set_flex_flow(cell, LV_FLEX_FLOW_COLUMN);
            lv_obj_set_flex_align(cell, LV_FLEX_ALIGN_CENTER,
                                  LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            lv_obj_t *vr = plain(cell);
            lv_obj_set_flex_flow(vr, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(vr, LV_FLEX_ALIGN_CENTER,
                                  LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            label(vr, cfg->sec[i].value, &lv_font_montserrat_18, PR_INK);
            lv_obj_t *cc = caret(vr, cfg->sec[i].trend, PR_GREY);
            if (cc) lv_obj_set_style_pad_left(cc, 3, 0);
            label(cell, cfg->sec[i].label, &lv_font_montserrat_12, PR_GREY);
        }
        if (cfg->spark_pts && cfg->spark_n > 1) {
            lv_obj_t *sp = build_spark(card, cfg);
            lv_obj_align(sp, LV_ALIGN_BOTTOM_MID, 0, -36);
        }
    } else {
        if (cfg->target) {
            lv_obj_t *t = label(card, cfg->target, &lv_font_montserrat_12, PR_DIM);
            lv_obj_align(t, LV_ALIGN_BOTTOM_MID, 0, -4);
        }
        if (cfg->spark_pts && cfg->spark_n > 1) {
            lv_obj_t *sp = build_spark(card, cfg);
            lv_obj_align(sp, LV_ALIGN_BOTTOM_MID, 0, -20);
        }
    }

    return card;
}

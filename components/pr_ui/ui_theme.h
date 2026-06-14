/* ui_theme.h — PrintedReef Desktop Display design tokens (shared pr_ui).
 *
 * Faithful LVGL 9.5 translation of design/phase-1-spec.md §2.
 * Colours, fonts and shared styles live here so every screen and every device
 * pulls from one source.
 */
#pragma once
#include "lvgl.h"
#include "../../design/tokens.h"   /* shared AS_COL_* design tokens (tokens.json) */

#ifdef __cplusplus
extern "C" {
#endif

/* ---- palette — sourced from the shared design tokens -------------------
 * Local PR_* NAMES are unchanged so no call site needs editing; each VALUE now
 * aliases an AS_COL_* token from design/tokens.h — the single source of truth,
 * generated from tokens.json. Mapping is by meaning.
 *
 * Status colours stay FUNCTIONAL, not brand: teal = in-range, amber =
 * attention, red = alert. Cyan is a neutral accent only. Never green.
 *
 * NOTE: the shared tokens are the platform baseline, NOT the old IPS-specific
 * tuning. The on-IPS colours shift slightly toward the shared palette. If the
 * IPS panel needs brighter values again, override individual PR_* AFTER this
 * block rather than re-hardcoding hexes.
 */
#define PR_TEAL    lv_color_hex(0x36D6C2)        /* in-range/nominal — KEPT LOCAL: AS_COL_OK==AS_COL_ACCENT would merge teal into PR_CYAN */
#define PR_AMBER   lv_color_hex(AS_COL_WARN)     /* attention                */
#define PR_RED     lv_color_hex(AS_COL_ERROR)    /* alert                    */
#define PR_CYAN    lv_color_hex(AS_COL_ACCENT)   /* neutral accent ONLY      */
#define PR_INK     lv_color_hex(AS_COL_TEXT)     /* primary text             */
#define PR_GREY    lv_color_hex(AS_COL_MUTED)    /* secondary text / labels  */
#define PR_DIM     lv_color_hex(AS_COL_DIM)      /* faint labels             */
#define PR_LINE    lv_color_hex(AS_COL_TRACK)    /* dividers, tile borders, gauge track */
#define PR_BG      lv_color_hex(AS_COL_BG)       /* screen background        */
#define PR_PANEL   lv_color_hex(AS_COL_CARD)     /* hero card / tile surface */
#define PR_WELL    lv_color_hex(AS_COL_WELL)     /* recessed inner panels    */
#define PR_TOPBAR  lv_color_hex(AS_COL_TOPBAR)   /* top bar background       */

/* ---- shared styles — initialised once by pr_theme_init() ---------------- */
extern lv_style_t pr_st_screen;   /* root screen                            */
extern lv_style_t pr_st_card;     /* hero card / tile surface               */
extern lv_style_t pr_st_well;     /* recessed inner panel (chem cell, spark) */
extern lv_style_t pr_st_topbar;   /* top bar                                */
extern lv_style_t pr_st_press;    /* pressed-state feedback (teal tint+shrink)*/

void pr_theme_init(void);

/* Give a clickable object tap feedback: teal tint + slight shrink while held.
   Call after marking the object CLICKABLE. Safe on any widget. */
void pr_press_fx(lv_obj_t *obj);

/* ---- fonts -------------------------------------------------------------- */
#ifndef PR_FONT_HERO
  #define PR_FONT_HERO (&lv_font_montserrat_48)
#endif

#ifdef __cplusplus
}
#endif

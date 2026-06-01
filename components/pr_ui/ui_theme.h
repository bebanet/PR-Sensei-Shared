/* ui_theme.h — PrintedReef Desktop Display design tokens (shared pr_ui).
 *
 * Faithful LVGL 9.5 translation of design/phase-1-spec.md §2.
 * Colours, fonts and shared styles live here so every screen and every device
 * pulls from one source.
 */
#pragma once
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- palette — IPS tuning ----------------------------------------------
 * The desktop unit is an IPS panel, not OLED. The OLED palette (kept below
 * for reference) reads washed-out on IPS. The round 1.75" AMOLED sibling
 * should define its own OLED-tuned palette variant.
 *
 * Status colours are FUNCTIONAL, not the brand palette: teal = in-range,
 * amber = attention, red = alert. Cyan is a neutral accent only. Never green.
 *
 * OLED reference (do not use on the IPS panel):
 *   teal 0x2BB6A3 · amber 0xE8A93C · red 0xE25646 · cyan 0x4FC3D9
 *   ink 0xF2F2F2 · grey 0x8A9099 · dim 0x646B74 · line 0x243039
 *   bg 0x0C1116 · panel 0x141B23 · well 0x10161C · topbar 0x0D141A
 */
#define PR_TEAL    lv_color_hex(0x36D6C2)   /* in-range / nominal       */
#define PR_AMBER   lv_color_hex(0xFFB83D)   /* attention                */
#define PR_RED     lv_color_hex(0xFF6454)   /* alert                    */
#define PR_CYAN    lv_color_hex(0x55D8F0)   /* neutral accent ONLY      */
#define PR_INK     lv_color_hex(0xFFFFFF)   /* primary text             */
#define PR_GREY    lv_color_hex(0xA7AFBA)   /* secondary text / labels  */
#define PR_DIM     lv_color_hex(0x8B95A1)   /* faint labels             */
#define PR_LINE    lv_color_hex(0x31404C)   /* dividers, tile borders   */
#define PR_BG      lv_color_hex(0x101820)   /* screen background        */
#define PR_PANEL   lv_color_hex(0x18232D)   /* hero card / tile surface */
#define PR_WELL    lv_color_hex(0x121D26)   /* recessed inner panels    */
#define PR_TOPBAR  lv_color_hex(0x131E28)   /* top bar background       */

/* ---- shared styles — initialised once by pr_theme_init() ---------------- */
extern lv_style_t pr_st_screen;   /* root screen                            */
extern lv_style_t pr_st_card;     /* hero card / tile surface               */
extern lv_style_t pr_st_well;     /* recessed inner panel (chem cell, spark) */
extern lv_style_t pr_st_topbar;   /* top bar                                */

void pr_theme_init(void);

/* ---- fonts -------------------------------------------------------------- */
#ifndef PR_FONT_HERO
  #define PR_FONT_HERO (&lv_font_montserrat_48)
#endif

#ifdef __cplusplus
}
#endif

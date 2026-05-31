/* ui_theme.c — shared LVGL styles for the PrintedReef fleet. */
#include "ui_theme.h"

lv_style_t pr_st_screen;
lv_style_t pr_st_card;
lv_style_t pr_st_well;
lv_style_t pr_st_topbar;

void pr_theme_init(void)
{
    /* root screen — flat dark ground, default text colour/font */
    lv_style_init(&pr_st_screen);
    lv_style_set_bg_color(&pr_st_screen, PR_BG);
    lv_style_set_bg_opa(&pr_st_screen, LV_OPA_COVER);
    lv_style_set_text_color(&pr_st_screen, PR_INK);
    lv_style_set_text_font(&pr_st_screen, &lv_font_montserrat_16);
    lv_style_set_pad_all(&pr_st_screen, 0);
    lv_style_set_border_width(&pr_st_screen, 0);
    lv_style_set_radius(&pr_st_screen, 0);

    /* card / tile surface */
    lv_style_init(&pr_st_card);
    lv_style_set_bg_color(&pr_st_card, PR_PANEL);
    lv_style_set_bg_opa(&pr_st_card, LV_OPA_COVER);
    lv_style_set_border_color(&pr_st_card, PR_LINE);
    lv_style_set_border_width(&pr_st_card, 1);
    lv_style_set_radius(&pr_st_card, 12);

    /* recessed well — chem cells, sparkline panel */
    lv_style_init(&pr_st_well);
    lv_style_set_bg_color(&pr_st_well, PR_WELL);
    lv_style_set_bg_opa(&pr_st_well, LV_OPA_COVER);
    lv_style_set_border_color(&pr_st_well, PR_LINE);
    lv_style_set_border_width(&pr_st_well, 1);
    lv_style_set_radius(&pr_st_well, 9);

    /* top bar — dark, with a bottom hairline */
    lv_style_init(&pr_st_topbar);
    lv_style_set_bg_color(&pr_st_topbar, PR_TOPBAR);
    lv_style_set_bg_opa(&pr_st_topbar, LV_OPA_COVER);
    lv_style_set_radius(&pr_st_topbar, 0);
    lv_style_set_border_side(&pr_st_topbar, LV_BORDER_SIDE_BOTTOM);
    lv_style_set_border_color(&pr_st_topbar, PR_LINE);
    lv_style_set_border_width(&pr_st_topbar, 1);
}

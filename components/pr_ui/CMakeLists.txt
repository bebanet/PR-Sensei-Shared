# pr_ui — shared LVGL widgets + theme for the PrintedReef fleet.
idf_component_register(
    SRCS
        "ui_theme.c"
        "ui_gauge.c"
    INCLUDE_DIRS "."
    REQUIRES lvgl__lvgl
)

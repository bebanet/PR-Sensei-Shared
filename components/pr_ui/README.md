# pr_ui — reusable LVGL widgets & theme tokens

The shared look. Widgets are display-agnostic; each device composes them into
its own screens with its own palette tuning.

**Will contain** (lifted from `PR-Desktop-Display-P4/components/ui/`):
- `ui_theme.*` — palette + shared styles (IPS and OLED reference palettes)
- `ui_gauge.*` — 270° arc card (dot marker, target ticks, sparkline)
- stat card · checklist · `ui_tile.*` · sparkline helper

**Excludes** whole-screen assemblies (`ui_reefhealth`, `ui_devices`,
`ui_glance`) — those are a *specific product's* layout and live in the device
repo. The round 1.75″ display reuses these widgets but builds its own round
screens + OLED palette.

`idf_component_register(... REQUIRES lvgl pr_contract)`

> **Migration status:** lives in `PR-Desktop-Display-P4` today; lifted here with
> the same atomic rewire as `pr_data`. See `../../ARCHITECTURE.md`.

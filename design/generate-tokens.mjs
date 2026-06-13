#!/usr/bin/env node
// Generates tokens.h (firmware / LVGL) and tokens.css (app) from tokens.json.
// The only file anyone edits is tokens.json. Run: node design/generate-tokens.mjs
import { readFileSync, writeFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { dirname, join } from "node:path";

const here = dirname(fileURLToPath(import.meta.url));
const tokens = JSON.parse(readFileSync(join(here, "tokens.json"), "utf8"));

const hex0x = (h) => "0x" + h.replace("#", "").toUpperCase();

const c = tokens.color;
const d = tokens.surface.device;
const a = tokens.surface.app;

// ---- tokens.h : semantic + device surfaces ----
const hLines = [
  "// AUTO-GENERATED from tokens.json by generate-tokens.mjs. Do not edit.",
  "#pragma once",
  "",
  `#define AS_COL_ACCENT   ${hex0x(c.accent)}`,
  `#define AS_COL_VIOLET   ${hex0x(c.violet)}`,
  `#define AS_COL_OK       ${hex0x(c.status.ok)}`,
  `#define AS_COL_WARN     ${hex0x(c.status.warn)}`,
  `#define AS_COL_ERROR    ${hex0x(c.status.error)}`,
  `#define AS_COL_INFO     ${hex0x(c.status.info)}`,
  `#define AS_COL_PAUSE    ${hex0x(c.status.pause)}`,
  `#define AS_COL_TEXT     ${hex0x(c.text.primary)}`,
  `#define AS_COL_MUTED    ${hex0x(c.text.muted)}`,
  `#define AS_COL_DIM      ${hex0x(c.text.dim)}`,
  `#define AS_COL_BG       ${hex0x(d.bg)}`,
  `#define AS_COL_CARD     ${hex0x(d.card)}`,
  `#define AS_COL_WELL     ${hex0x(d.well)}`,
  `#define AS_COL_TOPBAR   ${hex0x(d.topbar)}`,
  `#define AS_COL_BORDER   ${hex0x(d.border)}`,
  `#define AS_COL_TRACK    ${hex0x(d.track)}`,
  "",
];
writeFileSync(join(here, "tokens.h"), hLines.join("\n"));

// ---- tokens.css : semantic + app surfaces + light mode ----
const grad = `linear-gradient(160deg, ${a.bgGradient.join(", ")})`;
const cssLines = [
  "/* AUTO-GENERATED from tokens.json by generate-tokens.mjs. Do not edit. */",
  ":root {",
  `  --accent: ${c.accent};`,
  `  --violet: ${c.violet};`,
  `  --status-ok: ${c.status.ok};`,
  `  --status-warn: ${c.status.warn};`,
  `  --status-error: ${c.status.error};`,
  `  --status-info: ${c.status.info};`,
  `  --status-pause: ${c.status.pause};`,
  `  --text: ${c.text.primary};`,
  `  --text-muted: ${c.text.muted};`,
  `  --text-dim: ${c.text.dim};`,
  `  --bg: ${grad};`,
  `  --card: ${a.card};`,
  `  --border: ${a.border};`,
  "}",
  ':root[data-theme="light"] {',
  `  --bg: ${a.light.bg};`,
  `  --sheet-bg: ${a.light.sheet};`,
  `  --accent: ${a.light.accent};`,
  `  --text: ${a.light.text};`,
  `  --status-error: ${a.light.error};`,
  "}",
  "",
];
writeFileSync(join(here, "tokens.css"), cssLines.join("\n"));

console.log("Generated tokens.h and tokens.css from tokens.json");

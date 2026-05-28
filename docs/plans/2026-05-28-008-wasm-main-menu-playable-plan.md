---
title: "feat: WASM browser reaches KotOR main menu"
type: feat
status: completed
date: 2026-05-28
---

# feat: WASM browser reaches KotOR main menu

## Summary

Ship a vertical slice where `engine.html` served locally reaches the **main menu** in Chromium with lazy game FS (`gamefs.js` + manifest + ranged `/game-files/`). Fix any build, serve, shader, resource I/O, or runtime errors blocking that path.

## Requirements

- R1. `serve.py` + `build-web/bin/engine.html` loads without fatal console errors.
- R2. With a valid KotOR install via `--game-root` + `gen_game_manifest.py --strict`, engine reaches **main menu** (visual or Playwright assertion).
- R3. Document exact repro commands in `tools/web/progress.md`.
- R4. Push fixes on `cursor/web-wasm-gles-and-fs-access` and `origin/master` (no new PRs).

## Out of scope

- openkotor-site iframe integration polish.
- Full gameplay past menu.

## Test scenarios

| ID | Scenario | Pass criteria |
|----|----------|---------------|
| T1 | No game root | `engine.html` loads; clear message or black canvas without hang |
| T2 | With game root | Menu visible within 120s; no uncaught exception |
| T3 | `smoke_engine_menu.py` | Exit 0 when `--game-root` set |

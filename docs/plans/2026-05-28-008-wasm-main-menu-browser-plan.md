---
title: "feat: Reach KotOR main menu in browser WASM build"
type: feat
status: completed
date: 2026-05-28
---

# feat: Reach KotOR main menu in browser WASM build

## Summary

Ship a path where `engine.html` loads in the browser, reads game data (lazy FS or staging), and renders the **main menu** without manual hacks. Fix any engine, serve, preload, or JS glue errors blocking that.

## Requirements

- R1. `serve.py` + `build-web/bin/engine.html` serves and returns 200 for engine + wasm assets.
- R2. Game manifest + ranged `/game-files/` work (`test_serve_smoke.py` green).
- R3. With a local KotOR install (discover or `REONE_GAME_PATH`), `gen_game_manifest.py --strict` succeeds.
- R4. Browser run reaches main menu OR a documented fix lands for the blocking error (console/WASM).
- R5. `smoke_engine_menu.py` passes when game root is available (Playwright).

## Out of scope

- openkotor-site production deploy (follow-up).
- Full gameplay past main menu.

## Key files

- `tools/web/serve.py`, `tools/web/gamefs.js`, `tools/web/gen_game_manifest.py`
- `tools/web/smoke_engine_menu.py`
- `src/apps/engine/`, `src/libs/system/stream/webinput.cpp`
- `build-web/bin/engine.html`

## Test scenarios

| ID | Scenario | Expected |
|----|----------|----------|
| T1 | serve smoke | manifest 200, Range 206 |
| T2 | engine.html no game-root | clear error, not silent hang |
| T3 | engine.html + game-root | main menu visible within timeout |
| T4 | smoke_engine_menu.py | exit 0, screenshot shows menu chrome |

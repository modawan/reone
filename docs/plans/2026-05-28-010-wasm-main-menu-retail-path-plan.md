---
title: "feat: WASM browser path to KotOR main menu (retail install)"
type: feat
status: completed
date: 2026-05-28
---

# feat: WASM browser path to KotOR main menu (retail install)

## Summary

Reach the **main menu** in `engine.html` with a **retail KotOR install** via `serve.py --game-root` or FS Access. Fix startup crashes on stub/demo installs (invalid PE exe, TLK V1, truncated KEY). Reject GemRB demo roots in discovery; require real `chitin.key` + `swkotor.exe` sizes.

## Requirements

- R1. `discover_game_root.py` rejects GemRB/demo stubs; accepts retail-sized key/exe + TLK V3.0.
- R2. `ExeResourceContainer` / `Strings::init` do not abort engine on unreadable exe/tlk (web-tolerant).
- R3. Rebuild `build-web/bin` wasm; `test_serve_smoke.py` green.
- R4. `smoke_engine_menu.py` exit 0 when `REONE_WEB_SMOKE_GAME_ROOT` points at retail install.
- R5. Push fixes on `cursor/web-wasm-gles-and-fs-access` and `origin/master` (no new PRs).

## Out of scope

- Shipping retail game data in the repo.
- openkotor-site deploy.

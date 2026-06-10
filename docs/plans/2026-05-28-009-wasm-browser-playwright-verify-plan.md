---
title: "test: Verify WASM in browser via agent-browser + Playwright"
type: test
status: completed
date: 2026-05-28
---

# test: Verify WASM in browser via agent-browser + Playwright

## Summary

Pipeline-verify `build-web/bin/engine.html` with **both** `agent-browser` (ce-test-browser) and **Playwright** (`smoke_engine_menu.py`), served by `serve.py` with lazy game mirror.

## Requirements

- R1. Plan documents dual-tool browser gate (agent-browser + Playwright).
- R2. `serve.py` + wasm bundle HTTP 200; `test_serve_smoke.py` pass.
- R3. **agent-browser**: load `engine.html`, snapshot, screenshot, capture console via evaluate if needed.
- R4. **Playwright**: `tools/web/run_menu_smoke.sh` or `smoke_engine_menu.py --spawn-serve` with discovered game root.
- R5. Record results in `tools/web/progress.md`; push only if code fixes required.

## Test scenarios

| ID | Tool | Pass criteria |
|----|------|---------------|
| T1 | curl/serve smoke | manifest 200, Range 206 |
| T2 | agent-browser | engine.html loads, wasm assets 200, no fatal pageerror |
| T3 | Playwright | exit 0 when retail KotOR available; else clear error (not hang) |

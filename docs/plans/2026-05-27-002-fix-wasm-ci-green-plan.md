---
title: "fix: Green WASM CI and working browser engine bundle"
type: fix
status: completed
date: 2026-05-27
origin: docs/plans/2026-05-27-001-fix-wasm-pr-restore-debug-plan.md
---

# fix: Green WASM CI and working browser engine bundle

## Summary

Drive `cursor/web-wasm-gles-and-fs-access` to a **green** `Build WASM` workflow on OpenKotOR/reone and a verified `build-web/bin` bundle that loads through `serve.py` + lazy game FS. Prior pass restored the PR tip and hardened CI; this pass reproduces the wasm link locally (or via container), fixes compile/link/runtime blockers, and pushes fixes.

## Requirements

- R1. `build-wasm.yml` jobs `serve-smoke` and `wasm` succeed on head branch push.
- R2. `cmake --build build-web --target engine` produces `engine.html`, `engine.js`, `engine.wasm` passing `verify_wasm_bundle.py`.
- R3. `tools/web/test_serve_smoke.py` stays green.
- R4. Document or automate menu smoke path; run Playwright smoke when `engine` artifact exists.

## Implementation Units

- U1. **Reproduce wasm build** — install/use emsdk or Dockerfile.emscripten; capture first configure/link error.
- U2. **Fix build blockers** — CMake, missing symbols, Boost/SDL/Asyncify link flags.
- U3. **Harden CI workflow** — match local working configure flags; pin emsdk if needed; fix artifact paths.
- U4. **Validate serve + bundle** — `verify_wasm_bundle.py`, `test_serve_smoke.py`; optional `smoke_engine_menu.py` with stub/minimal game root.
- U5. **Push and confirm** — push branch; verify Actions when API available.

## Scope Boundaries

- No merge to modawan/reone master.
- No upstream/master merge on WASM branch.

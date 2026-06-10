---
title: "fix: Resolve modawan/reone#111 merge conflicts and re-verify CI"
type: fix
status: completed
date: 2026-05-28
---

# fix: Resolve modawan/reone#111 merge conflicts and re-verify CI

## Summary

Branch is **CONFLICTING** with `upstream/master`. Merge `upstream/master`, resolve conflicts preserving WASM work, verify locally, push **`origin/master`** directly (no PRs, stay on `cursor/web-wasm-gles-and-fs-access` until push).

## Requirements

- R1. Merge `upstream/master` into `cursor/web-wasm-gles-and-fs-access`; resolve all conflicts.
- R2. Local `verify_wasm_bundle.py` + `test_serve_smoke.py` pass after merge.
- R3. OpenKotOR **Build WASM** **success** on pushed HEAD (self-hosted `wasm-ci`).
- R4. Update `tools/web/progress.md` with merge status.
- R5. Push merged HEAD to `origin/master` (no new PRs; do not checkout other branches).

## Out of scope

- Playwright menu smoke (needs retail KotOR install).
- Marking PR ready-for-review (user decision while draft).

## Implementation units

| Unit | Files | Notes |
|------|-------|-------|
| Merge | `CMakeLists.txt`, others if any | Keep WASM options + upstream master changes |
| Verify | `tools/web/verify_wasm_bundle.py`, `tools/web/test_serve_smoke.py` | Narrow validation before push |
| Docs | `tools/web/progress.md` | PR mergeable status + CI run |

## Risks

- Large CMakeLists.txt conflict — review both sides for duplicate options.
- Push cancels in-flight `wasm-ci` via concurrency.

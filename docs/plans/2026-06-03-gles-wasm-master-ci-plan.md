# Post-GLES WASM on master — CI + smoke validation

**Date:** 2026-06-03  
**Status:** completed (this pass)

## Problem

OpenGL ES 3.0 landed on `master` (#7) but `build-wasm.yml` only ran on `cursor/**` pushes. GLES + WASM coexistence on `master` was unverified in CI; `tools/web/progress.md` still described self-hosted wasm-ci and stale concurrency guidance.

## Landed

- [x] Trigger `wasm-ci` on **`master`** pushes (path-filtered).
- [x] Refresh `tools/web/progress.md` (ubuntu-latest, cancel-in-progress: false, GLES #7, local smokes).
- [x] Harden `smoke_engine_menu.py` stdout when piped (`BlockingIOError`).
- [x] Local validation: `ci_build_wasm.sh`, menu smoke, `--warp end_m01aa`.

## Partial / human

- modawan/reone#163 — maintainer squash-merge still required.

## Next

- Close or merge `cursor/web-wasm-gles-and-fs-access` after diff review (branch diverged pre-GLES).
- Optional: add retail menu/warp smokes to CI (needs game fixture or skipped job).

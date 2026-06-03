# GLES PR #7 Merge Blockers — wasm-ci + Final Validation

**Date:** 2026-06-04  
**Status:** completed  
**Branch:** `glad-gles`  
**PRs:** [OpenKotOR/reone#7](https://github.com/OpenKotOR/reone/pull/7) · [modawan/reone#163](https://github.com/modawan/reone/pull/163)

## Problem

Upstream PR #7 has green **Build GLES engine** and **Linux/Windows build**, but **wasm-ci** is red. Failures are (a) `cancel-in-progress` aborting long self-hosted builds when PR events stack, and (b) self-hosted checkout auth flakes on re-run. Need repo-side workflow hardening, local wasm compile verification, and fresh CI.

## Scope

**In:**
- Harden `.github/workflows/build-wasm.yml` concurrency (no mid-build cancel on PR churn)
- Local `build-web` compile smoke (Emscripten available on dev host)
- Re-run GLES headless + menu smokes on HEAD
- Push fix, watch PR #7 checks, document unresolved infra in PR body if auth persists

**Out:** Self-hosted runner credential rotation (ops), master merge, modawan fork approval

## Requirements

| ID | Requirement | Verification |
|----|-------------|--------------|
| R1 | wasm workflow tolerates PR push churn | `cancel-in-progress: false` on PR builds |
| R2 | glad-gles still compiles for Emscripten | Local `cmake --build build-web --target engine` |
| R3 | GLES smokes still pass | `smoke_headless_selftest.sh` + menu smoke |
| R4 | PR #7 CI metadata current | `gh pr checks 7` after push |

## Files

- `.github/workflows/build-wasm.yml`
- `docs/plans/2026-06-03-gles-lfg-merge-ready-plan.md` (cross-link)

## Risks

- Self-hosted checkout auth may remain red until runner ops fixes token — document, don't mock skip wasm job

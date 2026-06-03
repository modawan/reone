# GLES glad-gles LFG Merge-Ready Completion Plan

**Date:** 2026-06-03  
**Status:** completed  
**Branch:** `glad-gles`  
**PR:** [modawan/reone#163](https://github.com/modawan/reone/pull/163) · upstream [OpenKotOR/reone#7](https://github.com/OpenKotOR/reone/pull/7)

## Pass 4 (2026-06-04)

- Moved wasm-ci to ubuntu-latest (self-hosted checkout auth broken); wasm green in 6m

- Re-validated headless + menu smokes on `ffd5a643` (Malak mean 929)
- Opened upstream PR OpenKotOR/reone#7 (`glad-gles` → `master`)

## Problem

The GLES migration slice (main menu PBR, cubemap IBL fallback, StreamMusic search, GUI preview lighting, headless smokes, Build GLES CI) is implemented on `glad-gles` with green OpenKotOR CI at `101b0aea`. An `/lfg` pass must confirm runtime parity, refresh PR/CI metadata, and leave the branch merge-ready without new feature scope.

## Scope

**In:**
- Re-run focused GLES smoke validation (headless selftest, menu, warp)
- Confirm OpenKotOR `Build GLES engine` CI on current HEAD
- Update PR #163 body with latest validation + CI links if stale
- Code review autofix for any safe doc/PR hygiene issues

**Out:**
- New renderer features, WASM work, master merge, fork workflow approval
- Committing untracked `tools/gles/evidence/` or glad generator scratch files

## Requirements traceability

| Req | Source | Verification |
|-----|--------|--------------|
| R1 Headless CI selftest | `tools/gles/smoke_headless_selftest.sh` | Exit 0 |
| R2 Main menu render + audio | `tools/gles/smoke_menu_validate.sh` | Malak mean ≥850, no theme load errors |
| R3 Module warp smoke | `tools/gles/smoke_warp_validate.sh` | Exit 0 for `danm14aa` |
| R4 OpenKotOR CI green | `.github/workflows/build-gles.yml` | `gh run list` success on HEAD |

## Files (read-only / metadata)

- `tools/gles/smoke_*.sh`
- `docs/plans/2026-05-30-gles-mainmenu-character-fix-plan.md`
- `docs/plans/2026-06-03-gles-warp-ibl-smoke-plan.md`

## Risks

- Local build missing → run `cmake --build build-gles` first
- Game path required for menu/warp smokes (Steam install)

## Test scenarios

1. `tools/gles/smoke_headless_selftest.sh` — no game, Xvfb
2. `GAME=... tools/gles/smoke_menu_validate.sh` — luminance + audio grep
3. `GAME=... MODULE=danm14aa tools/gles/smoke_warp_validate.sh` — warp + IBL log markers

## Completion criteria

- All three smokes pass locally
- OpenKotOR CI success on current HEAD (or re-push triggers green run)
- PR #163 body reflects HEAD SHA and CI URL
- No unresolved high-severity review findings without durable record

---
title: "fix: Get hosted Build WASM green on head SHA"
type: fix
status: active
date: 2026-05-28
deepened: 2026-05-28
---

# fix: Get hosted Build WASM green on head SHA

## Summary

OpenKotOR `Build WASM` jobs stay `queued` with zero steps while local `ci_build_wasm.sh` is green. R2 is done (concurrency + cancel superseded). Remaining blocker is **org-wide runner queue saturation** from other workflows on `master`.

## Requirements

- R1. Latest `wasm-ci` run on `cursor/web-wasm-gles-and-fs-access` completes **success** at head SHA.
- R2. At most one active/queued `Build WASM` run per branch — **done** (`concurrency` + `cancel-in-progress: true`).
- R3. Local `test_serve_smoke.py` and `ci_build_wasm.sh` remain passing.

## Findings (2026-05-28 deepen)

- `OpenKotOR/reone` had multiple simultaneous `queued` jobs: **CodeQL** (~90m), **Auto Assign** (~24m), **Build WASM** (~17m) — all with empty steps → shared runner pool, not a wasm compile failure.
- Mitigation: cancel stale non-wasm queued runs; avoid new pushes until `wasm-ci` completes; org admin may need to raise concurrency or fix stuck default-branch workflows.

## Implementation

1. Cancel stale queued runs on `master` (CodeQL, Auto Assign) when safe.
2. `gh run watch` on latest `Build WASM` for head `5612444c`.
3. If failure: fix workflow/build from logs; if success: mark R1 done, update PR #111 test plan.
4. Re-run local `./tools/web/ci_build_wasm.sh` on each workflow change.

## Out of scope

- Disabling CodeQL/Auto Assign permanently without maintainer approval.

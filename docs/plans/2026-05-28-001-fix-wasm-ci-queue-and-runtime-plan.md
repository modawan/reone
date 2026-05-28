---
title: "fix: Unblock WASM CI queue and validate runtime smoke"
type: fix
status: completed
date: 2026-05-28
origin: docs/plans/2026-05-27-002-fix-wasm-ci-green-plan.md
---

# fix: Unblock WASM CI queue and validate runtime smoke

## Summary

Prior pass verified local wasm build (emsdk 5.0.7). CI run `26546458543` remained **queued** on GitHub runners. This pass diagnoses workflow/queue issues, ensures `Build WASM` completes green, and adds lightweight runtime validation where possible without retail game assets.

## Requirements

- R1. `Build WASM` workflow completes with `serve-smoke` + `wasm` success on head branch.
- R2. Fix workflow issues causing stuck/cancelled runs (concurrency, job deps, runner limits).
- R3. Keep local reproduction path documented and passing.
- R4. Optional: Playwright menu smoke when game root + playwright available.

## Implementation Units

- U1. Diagnose CI run `26546458543` (or latest) — job logs, queue vs failure.
- U2. Fix workflow if needed (split jobs, reduce cancel-in-progress churn, workflow_dispatch test).
- U3. Re-push or re-run workflow; confirm green.
- U4. Runtime: serve smoke + verify bundle; menu smoke if tooling allows.

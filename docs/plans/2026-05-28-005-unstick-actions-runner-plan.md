---
title: "fix: Unstick OpenKotOR Actions runner dispatch for Build WASM"
type: fix
status: completed
date: 2026-05-28
---

# fix: Unstick OpenKotOR Actions runner dispatch for Build WASM

## Summary

`wasm-ci` stays `queued` with `runner_id: 0` on direct pushes to `OpenKotOR/reone`. Repo admin can force-cancel stuck runs and re-dispatch; if needed, toggle Actions off/on in repo settings.

## Requirements

- R1. `Build WASM` completes **success** on head `e7863d5f` (plan 004).
- R2. Document admin recovery steps in-repo (`doc/ci-actions-unblock.md`).
- R3. No further push churn until a dispatched run finishes or fails with logs.

## Implementation

1. `POST .../actions/runs/{id}/force-cancel` on stuck run.
2. `gh workflow run "Build WASM engine" --ref cursor/web-wasm-gles-and-fs-access`.
3. `gh run watch` until `success` or actionable failure.
4. On success: mark plan 004 completed; check PR #111 test plan box.

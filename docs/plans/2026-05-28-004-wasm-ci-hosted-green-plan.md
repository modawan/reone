---
title: "fix: Get hosted Build WASM green on head SHA"
type: fix
status: active
date: 2026-05-28
---

# fix: Get hosted Build WASM green on head SHA

## Summary

OpenKotOR `Build WASM` jobs reach `queued` with empty steps while local `ci_build_wasm.sh` is green. Dedupe competing workflow runs and restore **concurrency with `cancel-in-progress: true`** so only the latest push holds the queue slot.

## Requirements

- R1. Latest `wasm-ci` run on `cursor/web-wasm-gles-and-fs-access` completes **success** at head SHA.
- R2. At most one active/queued `Build WASM` run per branch (cancel superseded pushes).
- R3. Local `test_serve_smoke.py` and `ci_build_wasm.sh` remain passing.

## Implementation

1. Cancel stale `queued`/`in_progress` `Build WASM` runs on OpenKotOR/reone except the newest.
2. Add workflow `concurrency` group `wasm-ci-${{ github.ref }}` with `cancel-in-progress: true`.
3. Push once; watch run until `success` or capture failure logs.
4. Update PR #111 test plan when green.

## Out of scope

- Org billing / runner pool changes (document in PR if queue persists after dedupe).

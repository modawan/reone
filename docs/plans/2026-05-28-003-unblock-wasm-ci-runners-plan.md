---
title: "fix: Unblock GitHub Actions runner assignment for Build WASM"
type: fix
status: completed
date: 2026-05-28
---

# fix: Unblock GitHub Actions runner assignment for Build WASM

## Summary

`wasm-ci` jobs stay `queued` 30+ minutes with zero steps — local `ci_build_wasm.sh` is green. Remove workflow concurrency, cancel stale runs, dispatch a fresh workflow run, and document org-level runner limits if queue persists.

## Requirements

- R1. One `Build WASM` run reaches `in_progress` and completes `success` on head SHA.
- R2. No code regression; serve smokes + wasm link remain green locally.
- R3. Stop push storms that cancel runs before runners assign.

---
title: "chore: post-ship verification and plan hygiene"
status: completed
date: 2026-05-30
type: chore
---

# chore: post-ship verification and plan hygiene

## Summary

After PR #4 and PR #5 merged to `master`, verify runtime gates still pass, retire stale active plans, and sync deferred notes in completed plans.

## Requirements

1. Retail menu + module warp Playwright smokes pass on `master`
2. Self-hosted wasm runner online (or service installed)
3. Stale `status: active` plans superseded by shipped work marked `completed`
4. `2026-05-29-pykotor-installation-port-plan.md` deferred section reflects dataminer merge

## Progress (LFG pass 21 — shipped)

### Landed
- Menu smoke **PASS** (~32s); warp smoke **PASS** (`end_m01aa`, ~56s)
- Wasm runner **online** (`reone-wasm-BodenPC`)
- Retired stale plans 008/009 → `completed`; synced Installation port deferred notes
- PR #4 + PR #5 previously merged; no open PRs

### Next steps
- Pick next feature slice via `/ce-plan` (e.g. openkotor-site `/play` integration, dead container cleanup)

## Pass 22 (2026-05-30)

- Menu smoke **PASS** on `84ce961b`
- `tools/web/progress.md` PR status synced with merged #4/#5
- Runner **online**; `reone-wasm-runner.service` **active**

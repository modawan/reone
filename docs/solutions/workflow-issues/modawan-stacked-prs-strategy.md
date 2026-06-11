---
title: Split modawan downstream work into three stacked PRs, not a monolith
date: 2026-06-11
last_updated: 2026-06-11
last_refreshed: 2026-06-11
category: workflow-issues
module: modawan
problem_type: workflow_issue
component: development_workflow
severity: high
applies_when:
  - "Porting OpenKotOR work to modawan/reone where the source branch mixes loader, GLES, and WASM"
  - "A downstream PR exceeds reviewable scope or maintainer feedback asks for narrower slices"
symptoms:
  - "Agents or docs treat modawan PR #167 as the merge gate for all upstream work"
  - "LFG pass plans block on squash-merging a multi-thousand-line monolith"
root_cause: missing_workflow_step
resolution_type: workflow_improvement
related_components:
  - tooling
tags:
  - modawan
  - stacked-prs
  - pr-111-split
  - extract-layer
  - downstream-integration
---

# Split modawan downstream work into three stacked PRs, not a monolith

## Context

modawan/reone#111 closed as a monolith (~155 files: extract, GLES, WASM). Maintainers (Slack: PuritanWizard, modawan, Eldbury) asked for **three narrow downstream PRs** reviewed in order: resource loader, GLES, then WASM/browser. OpenKotOR had already landed the bundled work on `master`; treating modawan #167 (~12k lines) as the single merge target duplicated the monolith mistake and produced doc-only LFG passes that blocked on the wrong gate.

(session history) Prior sessions in this repo only contained the current investigation arc; no earlier session files matched this topic in the last seven days.

## Guidance

1. **Do not squash-merge modawan #167 as-is.** Hold the monolith; supersede it with stacked PRs linked from #111 and #167 comments.
2. **Slice by path bucket, not commit replay.** Cherry-pick the file set from `git diff upstream/master...origin/master` filtered to the bucket (~39 extract files for slice 1). OpenKotOR commits are interleaved with GLES/WASM.
3. **Branch from modawan `master`.** Example: `feat/modawan-extract` from `upstream/master` @ `48b2ea3e`, push to OpenKotOR fork, open PR to modawan with `--head OpenKotOR:feat/modawan-extract`.
4. **Stack order:** extract (Installation) → GLES → WASM. Each slice must exclude the next slice's paths (`tools/web/*`, `glsl/*`, `extern/glad/*` stay out of slice 1).
5. **Keep strategy docs off the modawan PR branch** when they are OpenKotOR-only planning artifacts; link them in the PR body instead.

## Why This Matters

Monolith PRs hide regressions (see the director save-scope bug in the extract slice), exhaust reviewers, and couple unrelated platform work. Narrow PRs allow Linux/Windows CI and benchmark feedback on the loader before GLES and WASM land.

## When to Apply

- Any modawan downstream port derived from `cursor/web-wasm-gles-and-fs-access` or OpenKotOR `master` when the diff spans multiple subsystems.
- When planning docs say "squash-merge #167" without naming which slice is in scope.

## Examples

**Wrong:** Single PR with extract + `tools/web` + GLES glad + shader changes; LFG pass 27 waits on #167 merge.

**Right:** Three open stacked PRs to modawan `master` — [PR #192](https://github.com/modawan/reone/pull/192) (extract/Installation, path-filtered loader), [PR #193](https://github.com/modawan/reone/pull/193) (GLES), [PR #194](https://github.com/modawan/reone/pull/194) (WASM). Merge in order; rebase each downstream branch onto merged `master` before the next slice is review-ready. Slice 1 test plan covers override order, module-root lazy index, `dialog.tlk`, and save-scope capsules (including director lifecycle fixes through `fba857dd5`).

**Verification ladder for slice 1:**

- `test/extract/installation.cpp` green via `ctest` (12 Installation tests locally)
- Module transition spot-check (native)
- PR body notes save UX still depends on modawan #165 / #168
- modawan Actions on fork heads may show `action_required` until a maintainer approves workflow runs — green CI on an older commit does not prove the latest head passed

## Related

- `STRATEGY.md` — three stacked modawan PRs
- `docs/brainstorms/2026-06-11-pr111-three-way-split-requirements.md` — R1–R14
- `docs/plans/2026-06-11-001-feat-modawan-extract-pr-plan.md` — slice 1 execution (completed)
- `docs/plans/_archive/wrong-direction-lfg-gate-2026-06-11/` — anti-pattern archive
- `docs/solutions/logic-errors/modawan-extract-director-save-scope-regression.md` — regression found during slice 1 review

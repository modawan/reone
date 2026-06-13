---
title: Close modawan extract slice 1 (PR #192 merge gate)
date: 2026-05-29
status: active
origin: docs/brainstorms/2026-06-11-pr111-three-way-split-requirements.md
plan: docs/plans/2026-06-11-001-feat-modawan-extract-pr-plan.md
---

# Close modawan extract slice 1 (PR #192 merge gate)

## Summary

Land P0/P1 correctness and Installation-level lifecycle tests on `feat/modawan-extract`, push to modawan PR #192, and get it merged before opening GLES (#193) or WASM (#194) for maintainer review. Rebase `optimize/extract-lookup` onto the extract branch and strip only the bench harness.

## Problem Frame

PR #192 is open with green CI but still carries director save-scope regressions at its remote head. Local fixes exist on `optimize/extract-lookup` but are not on the PR branch. Slice 1 cannot be called done until save scope survives module transition, save load preserves global capsules, and tests cover the director lifecycle sequence.

## Requirements

### Merge gate

- **R1.** Remove `loadGlobalResources()` from `ResourceDirector::onModuleLoad`; it runs only from `init()`.
- **R2.** `clearSaveScope()` resets `_customFolders` and `_customCapsules` to their global baselines.
- **R3.** Save load adds save folder and `savegame.sav` without dropping global custom capsules (shaderpack).
- **R4.** Texture lookup honors graphics quality; `SearchLocation::Lips` is in canonical search order.
- **R5.** `test/extract/installation.cpp` includes Installation-level tests simulating `onGameLoad` → `onModuleLoad` save/module scope (no ResourceDirector test required).
- **R6.** Fixes pushed to `OpenKotOR:feat/modawan-extract`; modawan PR #192 CI green on the new head.

### Delivery

- **R7.** Rebase or merge `optimize/extract-lookup` into `feat/modawan-extract`; strip `tools/extract/*` and root CMake bench wiring only.
- **R8.** Keep lookup index/caches (`_overrideIndex`, `_capsuleCache`, `_texturePackIndex`) if the branch builds and tests pass.

### Stack order

- **R9.** Merge #192 before treating #193 or #194 as ready for maintainer merge.
- **R10.** Do not merge modawan #167 as a monolith.

## Scope Boundaries

**In scope:** Correctness fixes, installation tests, push to #192, monitor until merged.

**Out of scope:** Plan/solution doc updates until after #192 merges; GLES/WASM slice work until #192 merges; full save/load UI (#165/#168).

**Deferred:** Benchmark harness on `optimize/extract-lookup`; doc amendments (R8–R12 from adversarial plan review).

## Success Criteria

- modawan PR #192 merged to `master`.
- PR #193 and #194 rebased on merged modawan `master` and mergeable when their turn arrives.
- No bench paths in the #192 diff.

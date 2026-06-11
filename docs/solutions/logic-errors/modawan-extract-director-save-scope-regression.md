---
title: ResourceDirector onModuleLoad wipes save scope during loadGame
date: 2026-06-11
last_updated: 2026-06-11
last_refreshed: 2026-06-11
category: logic-errors
module: resource
problem_type: logic_error
component: development_workflow
severity: critical
symptoms:
  - "Save-scoped folders and savegame.sav capsule are cleared when a module loads after onGameLoad"
  - "Nested save ERF resources may not resolve during from-save module load"
  - "Switching saves can leave stale save directories in customFolders"
root_cause: scope_issue
resolution_type: code_fix
related_components:
  - tooling
tags:
  - modawan
  - extract-layer
  - resource-director
  - save-scope
  - installation
---

# ResourceDirector onModuleLoad wipes save scope during loadGame

## Problem

The PyKotor `extract::Installation` port on `feat/modawan-extract` (modawan PR #192) regressed `ResourceDirector` scope handling versus upstream modawan `master`. Loading a save game prepares save-scoped resources, then module load cleared that scope before gameplay needed it.

**Status:** Fixed on `feat/modawan-extract` (modawan PR #192): core save-scope work in `3739e548`, capsule-cache eviction in `fba857dd5`. Verified locally with 12 `Installation*` tests. modawan CI workflows on the current head remain `action_required` until a maintainer approves fork PR runs.

## Symptoms

- `Game::loadGame()` calls `onGameLoad()` then `loadModule(..., fromSave=true)`; the module step invoked `onModuleLoad()`, which called `loadGlobalResources()` and cleared save scope.
- `clearSaveScope()` only cleared `_customCapsules`, not `_customFolders`, so consecutive save loads could accumulate folders.
- `loadSaveGameResources()` replaced the entire custom capsule list, dropping global capsules such as shaderpack.
- `textureSearchOrder()` always preferred TPA before TPB/TPC; upstream honored `GraphicsOptions::textureQuality`.
- `canonicalSearchOrder()` omitted `SearchLocation::Lips`; upstream mounted `lips/global.mod` at init.

## What Didn't Work

- Treating `loadGlobalResources()` as safe to call on every module transition — upstream only called it from `init()`.
- Partial save teardown — clearing capsules without folders mirrored neither old `_resources.clearSave()` nor complete Installation reset.
- Assuming Installation lazy loaders alone preserve upstream director semantics without a line-by-line parity check against `upstream/master:src/libs/resource/director.cpp`.

(session history) No prior session files outside the current thread documented this regression; it surfaced during ce-code-review comparison to upstream.

## Solution

**Landed in `3739e548` (+ follow-up `fba857dd5`):**

1. **`onModuleLoad`:** Removed `loadGlobalResources()`. Module load now clears provider caches and calls `loadModuleResources(name)` only.

2. **Global baseline:** `loadGlobalResources()` calls `setGlobalCustomFolders()` / `setGlobalCustomCapsules()` after clearing module and save scope.

3. **`clearSaveScope`:** Restores custom folders and capsules from global baselines (`fba857dd5` also clears the lazy capsule cache so an overwritten `savegame.sav` is not served from stale in-memory indexes).

4. **`loadSaveGameResources`:** Merges save folder and `savegame.sav` onto the existing custom lists instead of replacing them.

5. **Texture quality:** `textureSearchOrder(TextureQuality)` returns TPA, TPB, or TPC order based on graphics options; textures provider uses it.

6. **Lips:** `SearchLocation::Lips` added to `canonicalSearchOrder()`.

7. **Tests:** Four Installation-level lifecycle tests in `test/extract/installation.cpp` simulate `onGameLoad` → `onModuleLoad`, save switching, texture quality, and lips reachability.

## Why This Works

Save scope, module scope, and global scope are independent lifetimes. `onGameLoad` establishes save folders and `savegame.sav`; `onModuleLoad` should only set `_moduleRoot` and module capsule visibility. Reloading global/shader scope on module transition destroyed the save path established milliseconds earlier in `loadGame`. Clearing `_capsuleCache` on save-scope reset prevents in-memory capsule indexes from outliving overwritten save files.

## Prevention

Regression tests in `test/extract/installation.cpp`:

- After `onGameLoad`, call `onModuleLoad` and assert save folder + `savegame.sav` remain in Installation custom scope.
- Save switch: load save A, then save B; assert only B's folder is present.
- Texture quality: Medium setting resolves TPB when TPA and TPB both contain the resource.
- Lips: resource under `lips/` resolves via canonical order.

**Review gate:** When porting container stack to Installation, diff `director.cpp` against upstream merge-base and flag any new `loadGlobalResources()` call sites outside `init()`. When adding lazy capsule caches, evict them in every scope-reset path (`clearSaveScope`, `clearModuleScope`).

## Related Issues

- modawan [PR #192](https://github.com/modawan/reone/pull/192) — extract slice under review
- `docs/solutions/workflow-issues/modawan-stacked-prs-strategy.md` — why slice 1 stays narrow
- `docs/plans/2026-06-11-001-feat-modawan-extract-pr-plan.md` — save-scope test scenarios
- modawan #165, #168 — full save/load UX (out of slice 1 scope)

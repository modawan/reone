---
title: "feat: Save/load integration with extract Installation (modawan PR #192 follow-up)"
status: completed
date: 2026-06-12
type: feat
origin: docs/plans/2026-06-11-001-feat-modawan-extract-pr-plan.md
target_branch: feat/modawan-extract
target_pr: modawan/reone#192
---

# feat: Save/load integration with extract Installation

## Summary

Complete the save-game **load** path on `feat/modawan-extract` so `loadgame` restores player, party, globals, and module state from `Saves/<slot>/` via `extract::Installation` save scope — not vanilla game archives. Wire folder-based save slots in the SaveLoad GUI. Keep PR scope to extract-adjacent game/resource wiring; defer save **writing** and unrelated game logic fixes.

## Problem Frame

PR #192 landed PyKotor-style `Installation` resource resolution. Runtime testing showed `loadgame` warped to the correct module but loaded fresh game data (player missing). Root cause: game reset and party deserialize were not integrated with `Director::onGameLoad` / save-scope lookup. Lips packs (`lips/<module>_loc.{mod,erf}`) were also missing from module archive indexing.

## Requirements

| ID | Requirement |
|----|-------------|
| R1 | `Game::loadGame` resets runtime state, mounts save via `director.onGameLoad`, reads `savenfo`/`globalvars`, calls `onModuleLoad` while save scope active |
| R2 | Deserialize party from save module IFO + `partytable.res` + `availnpcN.utc`; restore inventory when present |
| R3 | `loadModule(..., fromSave=true)` skips default party spawn and uses save GIT/IFO |
| R4 | SaveLoad GUI uses folder slots `Saves/<slot>/`; `loadGame(number)` invokes `Game::loadGame(slotName)` |
| R5 | `Installation::loadModules` indexes lips loc capsules; test `loads_lips_loc_capsule_for_module` passes |
| R6 | `ctest` UnitTests green on Linux; modawan PR #192 CI green |
| R7 | PR body documents load path complete; save **write** deferred |

## Key Technical Decisions

- **KTD1 — Load order:** mount save → globals → `onModuleLoad(lastModule)` → party deserialize → `loadModule(fromSave=true)` (matches modawan upstream `game-reset` + `deserialize-party` intent).
- **KTD2 — Gold on party pool:** `_party.setGold` from `PT_GOLD`, not player creature gold field.
- **KTD3 — Save write out of scope:** GUI `saveGame` may warn-only; full ERF pack is follow-up.
- **KTD4 — No warp/party AI fixes:** `warp danm13` slow movement / missing NPCs are separate game logic, not this slice.

## Scope Boundaries

**In scope:** `game.cpp` load pipeline, party/combat reset, module `fromSave`, SaveLoad folder slots, lips indexing (already in extract layer).

**Out of scope:** Save writing, partytable builders, `Game::resetForLoad` mega-refactor, GLES/WASM, unrelated module warp bugs.

### Deferred to Follow-Up Work

- Implement `saveGame` ERF pack + metadata writers
- modawan #165/#168 full UX parity if still split upstream

## Implementation Units

### U1. Game load pipeline with save scope

**Goal:** Restore save state end-to-end on load.

**Requirements:** R1, R2, R3

**Dependencies:** none (extract layer on branch)

**Files:**
- `include/reone/game/game.h`
- `src/libs/game/game.cpp`
- `include/reone/game/party.h`
- `src/libs/game/party.cpp`
- `include/reone/game/combat.h`
- `src/libs/game/object/module.cpp`

**Approach:** Port upstream deserialize-party flow; ensure `onModuleLoad` before party reads save IFO/UTC.

**Test scenarios:**
- Happy path: load slot with `savenfo`, `globalvars`, `partytable`, nested `savegame.sav` restores player tag and last module
- Edge: missing `inventory.res` does not abort load
- Edge: missing `availnpcN.utc` logs warning, continues

**Verification:** Build succeeds; manual `loadgame 0` with retail save (documented in PR test plan).

---

### U2. SaveLoad GUI folder slots

**Goal:** Load menu reads `Saves/<slot>/` and calls `Game::loadGame`.

**Requirements:** R4

**Dependencies:** U1

**Files:**
- `include/reone/game/gui/saveload.h`
- `src/libs/game/gui/saveload.cpp`

**Approach:** Peek metadata from loose `.res` or `savegame.sav`; keep save action stub with warn.

**Test scenarios:**
- Happy path: slot directory with `savenfo.res` appears in list
- Edge: empty Saves dir shows empty list without crash

**Verification:** Compiles; no regression in existing unit tests.

---

### U3. Verify extract lips + unit tests

**Goal:** Confirm lips loc capsules indexed (PR #125 parity).

**Requirements:** R5, R6

**Dependencies:** none

**Files:**
- `test/extract/installation.cpp`
- `src/libs/resource/extract/installation.cpp`

**Test scenarios:**
- `InstallationModuleRoot.loads_lips_loc_capsule_for_module` passes

**Verification:** `ctest -R UnitTests` passes locally.

---

### U4. Ship on modawan PR #192

**Goal:** Commit, push, update PR description and CI.

**Requirements:** R6, R7

**Dependencies:** U1, U2, U3

**Files:** PR body only

**Verification:** `gh pr checks` green; PR documents load vs save scope.

## Risks and Mitigations

| Risk | Mitigation |
|------|------------|
| Save format edge cases | Warn-and-continue on optional resources |
| Scope creep into save write | Explicit deferral in PR body |
| CI Windows flake | Fix compile errors only; no assertion weakening |

## Open Questions

- **O1 (deferred):** When to implement save write — after #192 merges.

## Sources and Research

- modawan upstream branches: `game-reset`, `deserialize-party`
- modawan/reone#125 lips ERF
- Existing plan: `docs/plans/2026-06-11-001-feat-modawan-extract-pr-plan.md`

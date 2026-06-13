---
title: "fix: Save/load test coverage and path hardening"
status: active
date: 2026-05-29
type: fix
origin: docs/brainstorms/2026-05-29-save-game-writing-requirements.md
target_branch: feat/modawan-extract
---

# fix: Save/load test coverage and path hardening

## Summary

Close P0 gaps from security review and testing review on the landed save **load** slice: reject malicious save metadata paths, harden filesystem lookup, add director and Installation save-scope tests, and defer full save **writing** to a follow-up plan.

## Problem Frame

Save load landed without game-layer tests. Security review found crafted `LASTMODULE` can traverse outside the install root via `findFileIgnoreCase`. Testing review flagged missing `ResourceDirector::onGameLoad` coverage and Installation tests that bypass `appendSaveScope`.

## Requirements

| ID | Requirement |
|----|-------------|
| R1 | Reject invalid module and save slot names (non–ResRef-safe strings) before module/save resolution |
| R2 | `findFileIgnoreCase` / `getFileIgnoreCase` reject `.`, `..`, and empty path components |
| R3 | `ResourceDirector::onGameLoad` + `onModuleLoad` integration test with temp save slot |
| R4 | Installation save-scope tests use `appendSaveScope` production API |
| R5 | Unit tests for ResRef validation and path component rejection |
| R6 | `ctest` UnitTests green |

## Key Technical Decisions

- **KTD1 — ResRef validation** — KotOR module/save folder names use ResRef charset (alnum + `_`, max 16). Invalid names throw `ValidationException`.
- **KTD2 — Defense in depth** — Path lookup rejects traversal components even if a caller skips validation.
- **KTD3 — Save writing deferred** — Full retail save UX remains in `docs/brainstorms/2026-05-29-save-game-writing-requirements.md`; separate plan after merge gate is green.

## Implementation Units

### U1. Path and ResRef hardening

**Goal:** Block traversal via save metadata and unsafe lookup tokens.

**Requirements:** R1, R2, R5

**Files:**
- `include/reone/system/fileutil.h`
- `src/libs/system/fileutil.cpp`
- `src/libs/game/game.cpp`
- `src/libs/resource/director.cpp`
- `test/system/fileutil.cpp`

**Test scenarios:**
- `isValidResRef` accepts `end_m01aa`, rejects `../evil`, empty, overlong
- `findFileIgnoreCase` returns nullopt for `../x` paths

**Verification:** Unit tests pass.

---

### U2. Director save mount test

**Goal:** Exercise production `onGameLoad` → `appendSaveScope` wiring.

**Requirements:** R3

**Dependencies:** U1

**Files:**
- `test/resource/director.cpp`
- `test/CMakeLists.txt`

**Test scenarios:**
- Temp game root with `saves/slot000001/savegame.sav` and global `shaderpack.erf`
- After `onGameLoad("slot000001")`, Installation retains global capsule and appends save folder + sav

**Verification:** New test passes.

---

### U3. Installation appendSaveScope refactor

**Goal:** Align save-scope tests with production API.

**Requirements:** R4

**Files:**
- `test/extract/installation.cpp`

**Test scenarios:**
- Existing save-scope tests rewritten to call `appendSaveScope`
- New test: `appendSaveScope_merges_without_dropping_global_capsules`

**Verification:** Extract installation tests pass.

### Deferred to Follow-Up Work

- Full save game writing (`docs/brainstorms/2026-05-29-save-game-writing-requirements.md`)
- `Game::deserializeParty` unit tests (requires GFF fixtures)
- `SaveLoad` GUI filesystem tests

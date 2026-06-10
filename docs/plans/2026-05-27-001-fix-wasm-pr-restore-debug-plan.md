---
title: "fix: Restore WASM PR branch, fix build/CI, reach menu smoke"
type: fix
status: completed
date: 2026-05-27
origin: user request — undo mistaken local upstream merge; PR #111 must stay open
---

# fix: Restore WASM PR branch, fix build/CI, reach menu smoke

## Summary

PR [#111](https://github.com/modawan/reone/pull/111) on `modawan/reone` is **still OPEN (DRAFT)** — it was **not** merged on GitHub. The user-visible problem is a **local** `Merge modawan/reone master` commit that ballooned the branch (+68 commits) and mixed unfinished WIP. This plan resets `cursor/web-wasm-gles-and-fs-access` to the published PR tip, reapplies stashed WASM work, fixes the WASM engine + `tools/web` stack until menu smoke passes, and makes `.github/workflows/build-wasm.yml` green on `OpenKotOR/reone` PRs.

---

## Problem Frame

- User believed the PR was merged; only a **local merge** happened in a prior session.
- WASM CI last failed on Boost headers (`FindBoost` missing `Boost_INCLUDE_DIR`) on an older workflow revision.
- Stashed WIP on `master` may contain newer fixes not on the PR branch.
- `modawan/reone` PR view shows **no checks** (fork PR); CI must run on `OpenKotOR/reone` for the head branch.

---

## Requirements

- R1. Reset `cursor/web-wasm-gles-and-fs-access` to `origin/cursor/web-wasm-gles-and-fs-access` (remove local upstream merge).
- R2. Preserve and integrate stashed WASM WIP without re-merging `upstream/master` unless user asks.
- R3. `build-wasm.yml` passes: `serve-smoke` + `wasm` jobs (configure + build `engine`).
- R4. Local verification: `tools/web/test_serve_smoke.py` and documented menu path (`serve.py` + game root) when Emscripten is available.
- R5. Force-push updated branch so PR #111 reflects fixes only (no accidental merge commit).

---

## Scope Boundaries

- Do not merge PR #111 into `modawan/reone` master.
- Do not merge `upstream/master` into the WASM branch in this pass.
- Full in-game playability beyond main menu is out of scope unless blocking smoke.

---

## Key Technical Decisions

- **Reset over revert:** `git reset --hard origin/cursor/web-wasm-gles-and-fs-access` restores the PR tip cleanly.
- **Storage + game FS:** Keep upstream `Storage` pattern; path opens via `openGameInputStream` in `include/reone/resource/container/utils.h` (if still needed after reset, re-apply minimally).
- **CI Boost:** Workflow must pass `-DBoost_INCLUDE_DIR=/opt/reone/boost-include` after copying headers (already in current workflow file on branch).

---

## Implementation Units

- U1. **Restore branch tip**
  - **Goal:** Remove local merge commit `973048f3`.
  - **Files:** git state only.
  - **Verification:** `HEAD` equals `origin/cursor/web-wasm-gles-and-fs-access` (`3c3ae571`).

- U2. **Reapply stashed WASM WIP**
  - **Goal:** Bring forward work from `stash@{0}` without upstream merge.
  - **Files:** `CMakeLists.txt`, `tools/web/*`, `src/libs/system/stream/webinput.cpp`, shaders, engine, etc.
  - **Verification:** Clean working tree or resolved conflicts; no merge commit from upstream.

- U3. **Fix WASM build and resource I/O**
  - **Goal:** `emcmake` configure + `cmake --build build-web --target engine` succeeds.
  - **Files:** `CMakeLists.txt`, `include/reone/resource/container/utils.h`, containers, `gamefs.js`, `webinput.cpp`.
  - **Test scenarios:** CI configure finds Boost; engine links; no regression in `findResourceData` seek/read.

- U4. **Fix and validate GitHub Actions**
  - **Goal:** `build-wasm.yml` green on push to PR branch.
  - **Files:** `.github/workflows/build-wasm.yml`
  - **Test scenarios:** `serve-smoke` job passes; `wasm` job uploads `engine.{html,js,wasm}`.

- U5. **Runtime smoke (best effort)**
  - **Goal:** Menu smoke script or manual checklist documented if Emscripten missing locally.
  - **Files:** `tools/web/smoke_engine_menu.py`, `tools/web/test_serve_smoke.py`
  - **Verification:** Python smokes pass; menu smoke documented/partial if no game install.

---

## Risks & Dependencies

| Risk | Mitigation |
|------|------------|
| Stash conflicts with branch tip | Resolve file-by-file; prefer stash for `tools/web`, branch for unrelated |
| No Emscripten locally | Rely on CI + unit smokes |
| Fork PR checks invisible on modawan | Document that checks run on OpenKotOR fork |

---

## Sources & References

- PR: https://github.com/modawan/reone/pull/111
- `doc/webassembly.md`, `tools/web/progress.md`
- Failed run: OpenKotOR/reone `25281313956` (Boost not found)

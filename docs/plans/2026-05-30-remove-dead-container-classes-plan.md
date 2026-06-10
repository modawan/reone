---
title: "chore: remove dead KeyBif/Folder resource containers"
status: completed
date: 2026-05-30
type: chore
---

# chore: remove dead KeyBif/Folder resource containers

## Summary

Delete `KeyBifResourceContainer` and `FolderResourceContainer` — no callers remain after the `extract::Installation` port (PR #4/#5). **Phase B (follow-up):** migrate `saveload.cpp` off `ErfResourceContainer`, then remove Rim/Exe/Memory orphans.

## Inferred Intent

- **Direct ask:** `/lfg` on dead-container removal branch with repo archaeology.
- **Adjacent impact:** `peekSavedGame` is the sole `ErfResourceContainer` caller and omits `init()` — save-list UI may be broken; fix before deleting remaining containers.
- **Recommended cohesive scope:** Ship Phase A (KeyBif/Folder) now; Phase B = saveload → `LazyCapsule` + delete Rim/Exe/Memory/Erf stack.
- **Risks if only partially implemented:** Plan claiming Rim/Exe/Memory are "still used" blocks Phase B; leaving `peekSavedGame` unfixed hides a latent bug.

## Requirements

1. Remove `include/reone/resource/container/keybif.h`, `folder.h` and matching `.cpp` files
2. Drop entries from `src/libs/resource/CMakeLists.txt`
3. Linux + Windows CI green; wasm-ci green
4. Menu smoke PASS (no behavior change expected)

## Progress (LFG pass 26)

### Landed
- Phase A complete on branch: KeyBif/Folder removed (`2fb1a9be`)
- [OpenKotOR/reone#6](https://github.com/OpenKotOR/reone/pull/6) open + **MERGEABLE**
- Menu smoke **PASS** (~34s)
- Archaeology: only `ErfResourceContainer` has a caller (`saveload.cpp`); Rim/Exe/Memory are orphaned

### Phase B (2026-06-04)

- [x] Fix `peekSavedGame` → `ErfReader` + `openGameInputStream` (container never called `init()`)
- [x] Remove Rim/Exe/Memory/Erf containers + `IResourceContainer`

### Next steps

- CI green → merge Phase B PR

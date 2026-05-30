---
title: "chore: remove dead KeyBif/Folder resource containers"
status: active
date: 2026-05-30
type: chore
---

# chore: remove dead KeyBif/Folder resource containers

## Summary

Delete `KeyBifResourceContainer` and `FolderResourceContainer` — no callers remain after the `extract::Installation` port (PR #4/#5). Keep `ErfResourceContainer`, `RimResourceContainer`, `ExeResourceContainer`, and `MemoryResourceContainer` (still used).

## Requirements

1. Remove `include/reone/resource/container/keybif.h`, `folder.h` and matching `.cpp` files
2. Drop entries from `src/libs/resource/CMakeLists.txt`
3. Linux + Windows CI green; wasm-ci green
4. Menu smoke PASS (no behavior change expected)

## Progress (LFG pass 24)

### Landed
- (pending)

### Next steps
- Open PR, CI watch, merge

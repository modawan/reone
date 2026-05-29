---
title: "feat: PyKotor Installation resource resolution port"
status: active
date: 2026-05-29
type: feat
---

# feat: PyKotor Installation resource resolution port

## Summary

Replace reone's container-stack resource resolution with a C++ port of PyKotor's `Installation` / `FileResource` extract layer using PyKotor literal `SearchLocation` orders. Format Readers/Writers unchanged.

## Progress (2026-05-29 LFG pass 6)

### Landed
- Implemented `talkTableSearchOrder()` / `movieSearchOrder()` in `finder.cpp`
- `Strings::init` uses `talkTableSearchOrder()` (root-only `dialog.tlk`)
- `refreshCubeMap()` aligned with `refresh2D()` `#ifndef __EMSCRIPTEN__` brace pattern
- WASM rebuild + retail smoke **PASS** (menu + `end_m01aa` warp)

### Partial / uncertain
- Linux/Windows native CI still queued on self-hosted runners (backlog)
- Native `ctest` not run locally (openal)
- Container stack still in tree for dataminer

### Next steps
- Confirm Linux/Windows CI green after queue drains
- Retire container stack from hot path

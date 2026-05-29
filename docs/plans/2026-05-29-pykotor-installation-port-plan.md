---
title: "feat: PyKotor Installation resource resolution port"
status: active
date: 2026-05-29
type: feat
---

# feat: PyKotor Installation resource resolution port

## Summary

Replace reone's container-stack resource resolution with a C++ port of PyKotor's `Installation` / `FileResource` extract layer using PyKotor literal `SearchLocation` orders. Format Readers/Writers unchanged.

## Progress (2026-05-29 LFG pass 5)

### Landed
- Review autofix: `ResourceDirector` clears scope before setting shaderpack/glsl; save load appends folders
- `texture.cpp` native build fix (ifdef brace structure)
- Retail smoke **PASS** again after review commit (`9556a369`)
- OpenKotOR WASM CI **green** @ `9556a369`

### Partial / uncertain
- Linux/Windows native CI queued on `9556a369`
- Native `ctest` not run locally (openal)
- `talkTableSearchOrder` / `movieSearchOrder` declared but not implemented

### Next steps
- Linux CI green confirmation
- Implement missing finder helpers or remove dead declarations
- Retire container stack from hot path

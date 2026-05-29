---
title: "feat: PyKotor Installation resource resolution port"
status: active
date: 2026-05-29
type: feat
---

# feat: PyKotor Installation resource resolution port

## Summary

Replace reone's container-stack resource resolution with a C++ port of PyKotor's `Installation` / `FileResource` extract layer using PyKotor literal `SearchLocation` orders. Format Readers/Writers unchanged.

## Progress (2026-05-29 LFG pass 4)

### Landed
- **Smoke fix:** defer module capsule indexing until `module_root` is set (boot no longer reads all `modules/*.rim` over HTTP mirror)
- Search-order early exit in `Installation::locations` (first hit wins)
- Chitin/capsule EOF wrapped with path context; wasm magic-byte checks in verify + smoke preflight
- Retail smoke **PASS**: menu + `end_m01aa` warp (`REONE_WEB_SMOKE_GAME_ROOT` Steam install)
- Test: `InstallationModuleRoot.skips_module_index_until_module_root_set`

### Partial / uncertain
- Native `ctest` not run locally (openal)
- Container stack still in tree for dataminer
- `loadLips`/`loadRims` still eager-index all capsules (not on boot canonical path)

### Next steps
- OpenKotOR CI green on push
- Retire container stack from hot path
- Optional: lazy lips/rims indexing when module loads

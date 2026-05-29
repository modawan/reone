---
title: "feat: PyKotor Installation resource resolution port"
status: active
date: 2026-05-29
type: feat
---

# feat: PyKotor Installation resource resolution port

## Summary

Replace reone's container-stack resource resolution with a C++ port of PyKotor's `Installation` / `FileResource` extract layer using PyKotor literal `SearchLocation` orders. Format Readers/Writers unchanged.

## Progress (2026-05-29 LFG pass 2)

### Landed
- New `include/reone/extract/*` — `SearchLocation`, `finder`, `FileResource`, `LazyCapsule`, `Chitin`, `Installation`
- `Resources::find` / `get` delegate to `Installation` (`ResourceModule` owns and wires)
- `ResourceDirector` sets installation scope (`module_root`, custom folders/capsules) — no container mounts
- Providers: `Textures` / `AudioClips` use `textureSearchOrder()` / `soundSearchOrder()`; `Strings` loads `dialog.tlk` root-only via `resolveLooseRelativePath`; `Movies` via `Installation::moviePath`
- Root-only loose files: `resolveLooseRelativePath` never searches override/modules/chitin
- Unit tests: `test/extract/installation.cpp`, updated `test/resource/*`
- `tools/web/module_mirror.json` documents module archive paths (C++ `moduleArchiveRelPaths` parity)
- Wasm `cmake --build build-web --target engine` succeeds (Release)

### Partial / uncertain
- Native unit tests not run locally (missing `openal` dev package on agent host)
- Container stack (`container/*`) still in tree for dataminer/saveload; hot path removed from `Resources`
- `gamefs.js` still embeds `moduleMirrorRelPaths`; not yet loading `module_mirror.json`
- WASM menu/warp smoke not re-run (`REONE_WEB_SMOKE_GAME_ROOT` unset on agent)

### Next steps
- Run `tools/web/run_menu_smoke.sh` on retail install
- Wire `gamefs.js` to `module_mirror.json` or generate from one source
- Retire or test-only isolate remaining container code
- Native CI / install `openal` for `ctest` on extract tests

---
title: "feat: PyKotor Installation resource resolution port"
status: active
date: 2026-05-29
type: feat
---

# feat: PyKotor Installation resource resolution port

## Summary

Replace reone's container-stack resource resolution with a C++ port of PyKotor's `Installation` / `FileResource` extract layer using PyKotor literal `SearchLocation` orders. Format Readers/Writers unchanged.

## Progress (2026-05-29 LFG pass 8)

### Landed
- `moviePath()` uses `movieSearchOrder()` via `resolveLooseRelativePath` before `loadMovies()` map fallback
- Playwright smoke preflight closes stale `agent-browser` (fixes concurrent WebGL `SDL_GL_CreateContext` failure)
- Smoke `lsof` port-nuke timeout 60s → 5s (avoids multi-minute preflight stalls)
- Retail menu smoke **PASS** after browser preflight fix

### Partial / uncertain
- Linux/Windows native CI **queued** on self-hosted runners (backlog — `texture.cpp` fix already on branch)
- Container stack retirement deferred
- `movieSearchOrder` not yet wired through `locations()` for Override capsule paths (P3)

### Next steps
- CI green when runners drain; `force-cancel` stale queued runs if needed (`doc/ci-actions-unblock.md`)
- Retire container stack from dataminer-only path
- Native `ctest` when MAD/openal dev packages available locally

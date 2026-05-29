---
title: "feat: PyKotor Installation resource resolution port"
status: active
date: 2026-05-29
type: feat
---

# feat: PyKotor Installation resource resolution port

## Summary

Replace reone's container-stack resource resolution with a C++ port of PyKotor's `Installation` / `FileResource` extract layer using PyKotor literal `SearchLocation` orders. Format Readers/Writers unchanged.

## Progress (2026-05-29 LFG pass 9)

### Landed
- `fix(ci)`: `#include "reone/extract/installation.h"` in `resources.cpp` and `strings.cpp` (Emscripten incomplete-type)
- WASM CI **green** on push run `26648555012` (`6fd14994`)
- Playwright + agent-browser sequencing documented; smoke preflight kills stale browsers

### Partial / uncertain
- PR WASM run `26648559089` in progress at last check
- Linux/Windows native CI still **queued** (self-hosted backlog)
- Container stack retirement deferred

### Next steps
- `force-cancel` stale queued runs if backlog persists (`doc/ci-actions-unblock.md`)
- Retire container stack from dataminer-only path
- Rewrite vague `test` commit messages on squash merge (optional)

---
title: "feat: PyKotor Installation resource resolution port"
status: active
date: 2026-05-29
type: feat
---

# feat: PyKotor Installation resource resolution port

## Summary

Replace reone's container-stack resource resolution with a C++ port of PyKotor's `Installation` / `FileResource` extract layer using PyKotor literal `SearchLocation` orders. Format Readers/Writers unchanged.

## Progress (2026-05-29 LFG pass 10)

### Landed
- Windows CI test fixes: `GameID::KotOR` (not `K1`); provider tests seed via `Installation` override instead of removed `Resources::add`
- WASM CI green on `efb1ff67` / PR run `26649247933`

### Partial / uncertain
- Linux/Windows native CI in flight on self-hosted runners
- Container stack retirement deferred

### Next steps
- Confirm Linux/Windows green after test fix push
- Retire container stack from dataminer-only path

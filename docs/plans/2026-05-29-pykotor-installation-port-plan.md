---
title: "feat: PyKotor Installation resource resolution port"
status: active
date: 2026-05-29
type: feat
---

# feat: PyKotor Installation resource resolution port

## Summary

Replace reone's container-stack resource resolution with a C++ port of PyKotor's `Installation` / `FileResource` extract layer using PyKotor literal `SearchLocation` orders. Format Readers/Writers unchanged.

## Progress (2026-05-29 LFG pass 11)

### Landed
- `RimWriter` fixtures for module `.rim` tests (`41b0a470`) — fixes ctest RIM signature failures
- WASM CI **green** on PR run after RIM fix push

### Partial / uncertain
- Linux/Windows native CI running on `41b0a470` (prior failures were same RIM fixture bug)
- Container stack retirement deferred

### Next steps
- Confirm Linux/Windows green on `26650834410` / `26650834463`
- Retire container stack from dataminer-only path

---
title: "feat: PyKotor Installation resource resolution port"
status: active
date: 2026-05-29
type: feat
---

# feat: PyKotor Installation resource resolution port

## Summary

Replace reone's container-stack resource resolution with a C++ port of PyKotor's `Installation` / `FileResource` extract layer using PyKotor literal `SearchLocation` orders. Format Readers/Writers unchanged.

## Progress (2026-05-29 LFG pass 7)

### Landed
- Test clarity: `InstallationResolveLooseRelativePath` uses `talkTableSearchOrder()`
- Retail smoke **PASS** (re-run on `bcce4cf3` engine)
- WASM CI green on `9bd8db96`; `bcce4cf3` WASM in progress

### Partial / uncertain
- Linux/Windows native CI **queued** since `fdc50373` (self-hosted runner backlog — not a code failure)
- Local native configure blocked (MAD/openal dev packages missing on agent host)
- Container stack retirement deferred

### Next steps
- Linux/Windows CI completion after queue drains
- Wire `movieSearchOrder` into `moviePath` / `locations()` (P2)
- Retire container stack from dataminer-only path

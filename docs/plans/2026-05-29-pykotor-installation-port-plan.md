---
title: "feat: PyKotor Installation resource resolution port"
status: completed
date: 2026-05-29
type: feat
---

# feat: PyKotor Installation resource resolution port

## Summary

Replace reone's container-stack resource resolution with a C++ port of PyKotor's `Installation` / `FileResource` extract layer using PyKotor literal `SearchLocation` orders. Format Readers/Writers unchanged.

## Progress (2026-05-29 LFG pass 16 — shipped)

### Landed
- **[OpenKotOR/reone#4](https://github.com/OpenKotOR/reone/pull/4) merged** to `master` as `68d7f3ad` (squash)
- Engine/toolkit/tests use `extract::Installation` with PyKotor `SearchLocation` orders
- Retail menu smoke **PASS** (pass 16 re-validation, ~73s)
- Module warp smoke **PASS** (`--warp end_m01aa`, pass 14 artifact on master)

### Deferred (separate slices)
- Dataminer → `Installation` migration (still uses legacy containers)
- Headless canvas luminance probe (WebGL readback limitation; not a release gate)

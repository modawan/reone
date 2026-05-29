---
title: "feat: PyKotor Installation resource resolution port"
status: active
date: 2026-05-29
type: feat
---

# feat: PyKotor Installation resource resolution port

## Summary

Replace reone's container-stack resource resolution with a C++ port of PyKotor's `Installation` / `FileResource` extract layer using PyKotor literal `SearchLocation` orders. Format Readers/Writers unchanged.

## Progress (2026-05-29 LFG pass 15)

### Landed
- **All CI green** on `ecd72b2b` (`266555963*`) — menu + module warp smokes, Installation port
- Reopened [OpenKotOR/reone#4](https://github.com/OpenKotOR/reone/pull/4) (was closed without merge)
- Retail Playwright menu smoke **PASS** (pass 15 re-validation, ~79s)
- Module warp smoke **PASS** on `ecd72b2b` (`--warp end_m01aa`, pass 14)

### Partial / uncertain
- Headless canvas luminance probe near-black (known WebGL readback limitation)
- Dataminer container stack still uses legacy containers (deferred slice)

### Next steps
- **Merge PR #4** into `master`
- Dataminer → `Installation` migration (separate slice)

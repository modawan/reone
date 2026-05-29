---
title: "feat: PyKotor Installation resource resolution port"
status: active
date: 2026-05-29
type: feat
---

# feat: PyKotor Installation resource resolution port

## Summary

Replace reone's container-stack resource resolution with a C++ port of PyKotor's `Installation` / `FileResource` extract layer using PyKotor literal `SearchLocation` orders. Format Readers/Writers unchanged.

## Progress (2026-05-29 LFG pass 14)

### Landed
- **All CI green** on `179c9593` (`266537340*`) — PR merge-ready
- Retail Playwright menu smoke **PASS** (~91s)
- Module warp smoke **PASS** (`--warp end_m01aa`, ~98s to in-game; `EXIT:0`, `_smoke_warp_verify.png`)
- PyKotor `Installation` port slice complete for engine/toolkit/tests; dataminer container stack deferred

### Partial / uncertain
- Headless canvas luminance probe near-black (known WebGL readback limitation); warp success gated on `reoneWebModuleReady` + logs

### Next steps
- Merge [OpenKotOR/reone#4](https://github.com/OpenKotOR/reone/pull/4)
- Dataminer → `Installation` migration (separate slice)

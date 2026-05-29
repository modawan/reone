---
title: "feat: PyKotor Installation resource resolution port"
status: active
date: 2026-05-29
type: feat
---

# feat: PyKotor Installation resource resolution port

## Summary

Replace reone's container-stack resource resolution with a C++ port of PyKotor's `Installation` / `FileResource` extract layer using PyKotor literal `SearchLocation` orders. Format Readers/Writers unchanged.

## Progress (2026-05-29 LFG pass 12)

### Landed
- **All CI green** on `41b0a470`: WASM + Linux + Windows + CodeQL (`266508344*`)
- Retail Playwright menu smoke **PASS** (post-RIM-fix engine)
- Re-validation CI running on `9f972162` (docs-only chore)

### Partial / uncertain
- Container stack retirement deferred (dataminer still uses `KeyBifResourceContainer` etc.)

### Next steps
- Merge [OpenKotOR/reone#4](https://github.com/OpenKotOR/reone/pull/4) when ready
- Dataminer → `Installation` migration (separate slice)

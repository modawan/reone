---
title: "feat: PyKotor Installation resource resolution port"
status: active
date: 2026-05-29
type: feat
---

# feat: PyKotor Installation resource resolution port

## Summary

Replace reone's container-stack resource resolution with a C++ port of PyKotor's `Installation` / `FileResource` extract layer using PyKotor literal `SearchLocation` orders. Format Readers/Writers unchanged.

## Progress (2026-05-29 LFG pass 13)

### Landed
- **All CI green** on `1e2bfc49`: WASM + Linux + Windows + CodeQL (`266521221*`)
- Retail Playwright menu smoke **PASS** (re-run pass 13, ~75s; fresh `_smoke_menu_verify.png`)
- Stale warp smoke from pass 12 killed (hung `serve.py` + `tail` wrapper); menu gate is authoritative

### Partial / uncertain
- Module warp smoke (`--warp end_m01aa`) not completed in pass 12 (timeout/hang); defer to manual or longer timeout slice
- Container stack retirement deferred (dataminer still uses `KeyBifResourceContainer` etc.)

### Next steps
- Merge [OpenKotOR/reone#4](https://github.com/OpenKotOR/reone/pull/4) when ready
- Optional: module warp smoke with dedicated timeout budget
- Dataminer → `Installation` migration (separate slice)

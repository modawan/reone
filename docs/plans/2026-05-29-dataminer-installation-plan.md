---
title: "feat: dataminer Installation migration"
status: active
date: 2026-05-29
type: feat
---

# feat: dataminer Installation migration

## Summary

Replace legacy `KeyBifResourceContainer` / `FolderResourceContainer` / capsule container usage in `src/apps/dataminer` with `extract::Installation`, completing the container-stack retirement started in the Installation port.

## Requirements

1. All dataminer generators read game data via `Installation` + `FileResource`
2. Preserve prior lookup semantics (chitin-only vs override-then-chitin)
3. Fix pre-existing TSL nwscript bug (`k2KeyBif` used `k1KeyPath`)
4. Native build: `dataminer` target compiles; CI Linux/Windows green

## Progress (LFG pass 19)

### Landed
- Self-hosted runner `reone-wasm-BodenPC` brought **online** (was offline ~45m, wasm-ci queued)
- Linux + Windows CI **green** on `0272c2fa`
- Menu smoke **PASS** (pass 18)

### Partial / uncertain
- wasm-ci re-dispatch in progress after runner recovery

### Next steps
- wasm-ci green → merge [OpenKotOR/reone#5](https://github.com/OpenKotOR/reone/pull/5)

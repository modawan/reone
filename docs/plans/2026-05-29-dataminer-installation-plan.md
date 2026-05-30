---
title: "feat: dataminer Installation migration"
status: completed
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

## Progress (LFG pass 19 — shipped)

### Landed
- **[OpenKotOR/reone#5](https://github.com/OpenKotOR/reone/pull/5) merged** to `master` as `e8e4b678`
- All dataminer generators use `extract::Installation`; container stack retired in dataminer
- Self-hosted runner `reone-wasm-BodenPC` recovered (was offline); `reone-wasm-runner.service` installed for persistence
- All CI **green** (Linux, Windows, wasm-ci, CodeQL)

### Deferred
- None for this slice

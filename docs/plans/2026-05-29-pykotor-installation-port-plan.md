---
title: "feat: PyKotor Installation resource resolution port"
status: completed
date: 2026-05-29
type: feat
---

# feat: PyKotor Installation resource resolution port

## Summary

Replace reone's container-stack resource resolution with a C++ port of PyKotor's `Installation` / `FileResource` extract layer using PyKotor literal `SearchLocation` orders. Format Readers/Writers unchanged.

## Progress (2026-06-10 LFG pass 16)

### Landed
- Merged `origin/master` into `cursor/web-wasm-gles-and-fs-access` (GLES #15, dataminer Installation, handoff docs)
- **[OpenKotOR/reone#4](https://github.com/OpenKotOR/reone/pull/4) merged** to `master` as `68d7f3ad` (squash)
- Engine/toolkit/tests/dataminer use `extract::Installation` with PyKotor `SearchLocation` orders
- Branch retains post-merge WASM work (module warp, pass 11–15 smokes)

### Partial / uncertain
- Headless canvas luminance probe near-black (WebGL readback limitation; not a release gate)
- Stuck `wasm-ci` run `27273886615` queued 4h+ — force-cancel + redispatch after push

### Next steps
- Open fresh PR from synced branch → `master`
- Re-run menu + module warp smokes after merge commit
- modawan/reone#167 maintainer squash-merge (human gate)

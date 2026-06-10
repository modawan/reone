# Archive `cursor/web-wasm-gles-and-fs-access`

**Date:** 2026-06-04  
**Status:** completed

## Summary

WASM menu + module warp shipped on OpenKotOR `master` via PR #4, #10, and follow-ups. The long-lived feature branch diverged pre-GLES; critical paths (`gamefs.js`, `engine.cpp`, warp smoke) are identical on `master`.

## Landed

- [x] Delete orphaned `include/reone/resource/container.h` (implementations removed in #11)
- [x] Document branch supersession in `tools/web/progress.md`
- [x] Delete remote `cursor/web-wasm-gles-and-fs-access` on OpenKotOR/reone
- [x] Note modawan/reone#111 superseded by OpenKotOR `master`

## Human

- modawan/reone#163 — maintainer squash-merge

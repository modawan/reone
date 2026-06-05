# GLES LFG pass 10 — post #15 closure

**Date:** 2026-06-04  
**Status:** completed  
**OpenKotOR `master`:** `bd5b3bae` (#16 pass 10; includes #15 @ `d9aff290`)

## Shipped since pass 9

| PR | Change |
|----|--------|
| #14 | DialogGUI talk position guard for non-model nodes |
| #15 | GLES black model/texture fix (drawBuffers, DXT mips, OIT, smoke `bin/`) |

## This pass

1. Document pass 10 closure; update `tools/web/progress.md`
2. GLES smoke guard: require `build-gles/bin/engine`; warn on stale `debug/bin/engine`
3. Force-sync `glad-gles` → `master` for modawan/reone#163 refresh
4. Verify: `smoke_headless_selftest.sh`, menu smoke headless

## Landed (2026-06-05)

- [x] Pass 10 closure merged OpenKotOR #16 @ `bd5b3bae`
- [x] `OpenKotOR:glad-gles` force-synced to `master`; modawan #163 head @ `bd5b3bae`
- [x] modawan #163 / #166 PR bodies + sync comments refreshed
- [x] Headless menu smoke Malak torso mean ~6401–6531

## Human

- Squash-merge [modawan/reone#163](https://github.com/modawan/reone/pull/163) (CONFLICTING vs fork `master`; agent lacks write/merge on modawan)

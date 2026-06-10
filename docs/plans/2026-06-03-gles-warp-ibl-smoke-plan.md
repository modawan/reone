# GLES Warp / IBL Headless Smoke Plan

**Date:** 2026-06-03  
**Status:** completed  
**Branch:** `glad-gles`  
**Parent:** `docs/plans/2026-05-30-gles-cubemap-ibl-fallback-plan.md`

## Goal

Validate in-module PBR/IBL on the Mesa llvmpipe fallback path using headless Xvfb smoke, without touching the user's Wayland session.

## Landed

- `tools/gles/smoke_warp_validate.sh` uses `commands-file=smoke_warp.cmd` (`warp <module>`) instead of fragile xdotool GUI clicks on Xvfb
- `tools/gles/smoke_headless_selftest.sh` for CI (Xvfb + llvmpipe sanity, no game install)
- `.github/workflows/build-gles.yml` runs headless self-test after build
- Local warp smoke captures screenshot + log grep for IBL path (`Cube map array supported: no` on Mesa)

## Partial / uncertain

- Screenshot luminance not auto-scored (manual review of evidence PNGs)
- OES cubemap fast path still requires hardware GLES CI (not Mesa llvmpipe)

## Validation

```bash
cmake --build build-gles --target engine launcher shaderpack -j$(nproc)
tools/gles/smoke_headless_selftest.sh
GAME=/path/to/swkotor MODULE=danm14aa tools/gles/smoke_warp_validate.sh
```

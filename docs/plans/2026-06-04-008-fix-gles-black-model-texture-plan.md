---
title: "fix: GLES black model/texture rendering on real GPU"
status: completed
date: 2026-06-04
type: fix
origin: docs/plans/2026-05-30-gles-mainmenu-character-fix-plan.md
---

# fix: GLES Black Model/Texture Rendering on Real GPU

## Summary

Main menu 3D preview (Malak) and in-game models rendered as black silhouettes on real GPUs while GLES CI smokes passed. Root causes: stale `build-gles/debug/bin/engine` binary (fixes never ran), OIT mis-routing for GUI meshes, incomplete compressed-texture mip chains, stale `glDrawBuffers` MRT state, and a false-positive smoke crop.

## Landed (2026-06-04)

| Unit | Change | Verification |
|------|--------|--------------|
| U1 drawBuffers | Positional `glDrawBuffers` fills all `GL_MAX_DRAW_BUFFERS` slots | Log: `Max draw buffers: 8` |
| U2 textures | S3TC CPU fallback, BGR swizzle on cube/array, skip `glGenerateMipmap` on DXT + `GL_LINEAR` min-filter | Real GPU Malak mean ~6200–7000 |
| U3 OIT | GUI meshes opaque path; `f_oit_blend` fallback; transparent clear alpha 0 | Both pbr=0/1 menu smokes pass |
| U4 smoke | `build-gles/bin` not stale `debug/bin`; Malak crop `80x220+55+180` threshold ≥400; `dev=0` hides WARP | Headless mean ~6410; real GPU pass |

## Partial / uncertain

- Real-display PBR smoke can flake if stray `smoke_warp.cmd` or parallel engine runs remain; menu script now removes `$BINDIR/smoke_warp.cmd` and uses `dev=0`.

## Recommended next steps

- Delete or document stale `build-gles/debug/bin/` artifact
- `/ce-compound` capture institutional memory (no `docs/solutions/` yet)
- Optional: add menu smoke to CI with synthetic fixture (no retail `GAME=` path)

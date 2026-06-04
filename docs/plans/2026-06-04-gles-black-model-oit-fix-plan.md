# GLES Black Model / OIT Blend Fix Plan

**Date:** 2026-06-04  
**Status:** completed  
**Problem:** Main menu 3D preview (Malak) renders as a black silhouette on real GPU in both `pbr=0` and `pbr=1` (orthographic GUI uses retro forward path for both).

## Root cause [SYNTH]

1. **`MeshSceneNode::isTransparent()`** treated every DXT5/RGBA8 texture as transparent. Main-menu body meshes went through the OIT pass while the composite shader selects the **opaque** color buffer when accum alpha is 1 — but those meshes never drew into opaque, yielding black silhouettes on real GPU.
2. **OIT composite fallback** (`f_oit_blend.glsl`): guard when accum/revealage are both zero (uninitialized transparent targets).
3. **False-positive smoke gate**: crop at `+140+120` measured menu chrome, not Malak.

## Implementation

1. Fix `isTransparent()` — only Additive, partial `_alpha`, etc.; not `hasAlphaChannel()` alone.
2. Harden `f_oit_blend.glsl` — pass through opaque when no OIT weights written.
3. `smoke_menu_validate.sh` — crop left 3D view; threshold >= 850.

## Validation

```bash
GAME=/path/to/swkotor tools/gles/smoke_menu_validate.sh
REONE_USE_REAL_DISPLAY=1 GAME=/path/to/swkotor tools/gles/smoke_menu_validate.sh
```

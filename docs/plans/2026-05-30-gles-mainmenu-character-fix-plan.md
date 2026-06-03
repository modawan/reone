# GLES Main Menu Character Fix Plan

**Date:** 2026-06-03  
**Status:** completed  
**Branch:** `glad-gles`  
**Problem:** On launch, the main menu 3D preview (Malak) renders as a black silhouette in both retro (`pbr=0`) and PBR (`pbr=1`) modes on GLES.

## Root cause

`SceneInitializer::invoke()` always calls `setAmbientLightColor()` with its default `{0.0f}`, overwriting `SceneGraph`'s default ambient of `0.5f`. The retro forward shader multiplies diffuse texture by `(ambient + lit diffuse)`; with zero world ambient and insufficient active model lights, the character appears black except self-illum areas.

Both menu modes use the retro forward path for orthographic GUI cameras (see `chooseRendererType()` in `src/libs/scene/graph.cpp`).

## Scope

### In scope
- Fix GUI scene ambient so main menu (and other `SceneInitializer` previews) retain sensible default lighting
- Harden `tools/gles/smoke_menu_validate.sh`: always rebuild, verify engine alive, reliable window capture
- Build → validate loop with screenshot evidence before claiming fixed

### Out of scope (defer until menu verified)
- Warp/module smoke, OES cubemap path logging, CI autofix loop follow-ups

## Implementation

### 1. Preserve scene graph ambient unless explicitly set

**Files:** `include/reone/gui/sceneinitializer.h`, `src/libs/gui/sceneinitializer.cpp`

- Add `std::optional<glm::vec3> _ambientLightColor`
- Only call `setAmbientLightColor()` when `ambientLightColor()` was invoked
- Preserves `SceneGraph` default `0.5f` for all GUI 3D views

### 2. Harden menu smoke script + headless CI harness

**Files:** `tools/gles/smoke_menu_validate.sh`, `tools/gles/smoke_warp_validate.sh`, `tools/gles/headless_x11.sh`, `tools/gles/with_headless_x11.sh`, `.github/workflows/build-gles.yml`

- Isolated **Xvfb** display (`:199`–`:250`) with **Mesa llvmpipe** software GLES — never uses the user's Wayland `:0`
- Run `cmake --build` before launch; verify engine alive before screenshot
- Skip `xdotool windowactivate` on headless (no EWMH)
- CI deps: `xvfb`, `x11-utils`, `xdotool`, `imagemagick`

### 3. Default mesh material colors when MDL ambient/diffuse are zero

**File:** `src/libs/scene/node/mesh.cpp`

- Meshes with zero ambient/diffuse skip world-ambient contribution in retro shader (`uAmbientColor * uWorldAmbientColor`)
- Treat zero MDL material colors as white so GUI/main-menu body geometry is lit

### 4. Explicit main-menu ambient

**File:** `src/libs/game/gui/mainmenu.cpp`

- `SceneInitializer(...).ambientLightColor(glm::vec3(0.5f))` for K1 main menu 3D view

## Test scenarios

| Scenario | Expected |
|----------|----------|
| `pbr=1`, KOTOR main menu | Malak visible (not flat black); red saber glow; UI chrome present |
| `pbr=0`, same | Malak visible with retro lighting |
| Smoke script | Builds first, captures PNG, engine still running at capture time |
| Engine log | No `Loading module`; shaders compile |

## Validation command

Headless by default (isolated Xvfb on `:199`–`:250`, Mesa llvmpipe — never uses Plasma `:0`):

```bash
cmake --build build-gles --target engine launcher shaderpack -j$(nproc)
GAME=/path/to/swkotor tools/gles/smoke_menu_validate.sh
```

Optional:
- `REONE_SKIP_BUILD=1` — skip rebuild when binaries are already current
- `REONE_USE_REAL_DISPLAY=1` — opt into your real session (Wayland/X11) for local GPU debugging
- `tools/gles/with_headless_x11.sh <command>` — run any command in a disposable headless X session

Fedora deps: `sudo dnf install xorg-x11-server-Xvfb xdotool ImageMagick`

Evidence: `tools/gles/evidence/gles-menu-*.png`

**Note:** Software rendering (llvmpipe) may differ visually from your GPU; log checks and startup remain the primary CI gate until llvmpipe parity is improved.

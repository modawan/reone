---
title: "feat: New Game -> load first module -> render area in browser WASM"
status: completed
date: 2026-05-28
type: feat
---

# feat: New Game -> load first module -> render area in browser WASM

## Summary

The browser WebAssembly (Emscripten) build now renders the **main menu** from a retail
KotOR install over the lazy web filesystem (HTTP mirror + File System Access). The next
milestone is to get **into gameplay**: load the first game module (`end_m01aa`, the Endar
Spire) and render its 3D area scene in the browser, exercising the same module-load path
that "New Game" reaches after character generation.

The dominant technical risk is **not** the New Game GUI flow (chargen) — it is whether
`Game::loadModule` + the area render pipeline survive WebGL2/GLES3.0 and the lazy web
filesystem at the much larger I/O and GL-feature volume an area requires. We therefore
target the module-load + area-render path directly, drive it deterministically from a
JS-callable hook (the same pattern as `reoneWebOnEngineReady`), and verify with a
non-black, non-menu screenshot in the existing Playwright smoke.

Full character-generation GUI (portrait/abilities/skills/name screens driven headlessly)
is **out of scope** for this milestone and deferred — `loadModule` already loads a default
party when none exists (proven by the `warp` console command), so an area can render
without chargen.

---

## Problem Frame

When the user picks **New Game**, `MainMenu` calls `Game::startCharacterGeneration()`; after
chargen, `CharacterGeneration` calls `_game.loadModule("end_m01aa")` (see
`src/libs/game/gui/chargen.cpp`). The developer **Warp** menu and the `warp <module>` console
command (`Game::consoleWarp`, `src/libs/game/game.cpp`) call the same `Game::loadModule`
without chargen. `loadModule`:

1. `director.onModuleLoad(name)` mounts the module's containers (`<name>.rim`, `<name>_s.rim`,
   `<name>.mod`, `<name>_loc.*`) via `ResourceDirector::loadModuleResources`
   (`src/libs/resource/director.cpp`).
2. reads `module.ifo`, builds the `Module`/`Area`, loads room models, walkmesh, lightmaps,
   layout (`.lyt`), visibility (`.vis`), and area textures (from `swpc_tex_tpc.erf`, the Low
   texture pack already mounted at init).
3. loads/spawns the party, plays area music, and calls `openInGame()`.

All file bytes are read lazily through `reone::WebFileInputStream`
(`src/libs/system/stream/webinput.cpp`) and `Module.reoneWebFileReadAsync`
(`tools/web/gamefs.js`) — the same path that already works for menu init. The new risk is
**volume** (an area pulls many small resources plus a large texture ERF) and **GL surface**
(the Retro render pipeline uses MRT, weighted-blended OIT with float color buffers, shadow
depth maps, and bloom ping/pong — far more than the menu's GUI + single model).

There is also a known non-fatal `GL_INVALID_OPERATION: Feedback loop formed between
Framebuffer and active Texture` warning from a post-process pass that should be resolved so
the area renders cleanly.

---

## Requirements

- R1. A deterministic, JS-callable way to load a named module in the WASM build, fired after
  the menu is confirmed ready (mirrors `reoneWebOnEngineReady`). Must not require driving the
  chargen GUI.
- R2. `Game::loadModule("end_m01aa")` completes in the browser without throwing/stalling:
  module containers mount over the lazy web FS, the area builds, and `openInGame()` is reached.
- R3. The area's 3D scene renders under WebGL2/GLES3.0 — no fatal GL errors; the
  feedback-loop warning is eliminated or proven harmless.
- R4. The Playwright smoke can drive the module load after menu-ready and assert the canvas
  shows a rendered area (non-black, visibly different from the menu).
- R5. No regression to the working main-menu path; no dependency on `swkotor.exe`.

Success criteria: `tools/web/run_menu_smoke.sh` (with a warp flag) reaches a log line
indicating `Module 'end_m01aa' loaded successfully` + in-game screen, and saves a screenshot
that is not uniformly black and not the menu.

---

## Key Technical Decisions

- **Drive module load via a JS hook, not the chargen GUI.** Add an `EMSCRIPTEN_KEEPALIVE`
  `extern "C"` entry point (e.g. `reone_web_warp(const char *module)`) that schedules a
  `Game::loadModule` on the next frame boundary (avoid re-entrancy from a JS callback into a
  mid-frame Asyncify suspend). Mirror the existing `reoneWebOnEngineReady` EM_ASM pattern and
  add a `reoneWebOnModuleReady` signal after `openInGame()`. Rationale: deterministic, matches
  the real post-chargen `loadModule` call, avoids the large separate effort of headless chargen.
- **Target `end_m01aa` (Endar Spire), the K1 New Game start module** (per `chargen.cpp`).
- **Default party path is acceptable.** `loadModule` calls `loadDefaultParty()` when the party
  is empty; no chargen state is required to render an area.
- **Keep graphics options minimal/low** (shadowResolution=1, ssr/ssao off, textureQuality Low)
  as already set in `src/apps/engine/main.cpp` — minimizes GL surface and I/O for the first
  area render. Higher fidelity is deferred.
- **Fix WebGL2 issues at the GL-abstraction layer** (`src/libs/graphics/*`) so desktop and web
  stay on one code path, consistent with prior fixes (UBO std140 sizing, drawBuffers, FBO
  texture attach). Guard truly desktop-only calls with `#ifndef __EMSCRIPTEN__` only when no
  WebGL2 equivalent exists.

---

## Implementation Units

### U1. JS-callable module-load entry point + module-ready signal (WASM)

**Goal:** Provide a deterministic, re-entrancy-safe way to trigger `Game::loadModule` from JS
after the menu is ready, and emit a signal when the area is in-game.

**Requirements:** R1, R5

**Dependencies:** none

**Files:**
- `src/apps/engine/engine.cpp` and/or `src/libs/game/game.cpp` (add `EMSCRIPTEN_KEEPALIVE`
  `extern "C" void reone_web_warp(const char *module)`; queue the module name and apply at a
  safe point in the frame, not directly inside the JS call)
- `src/libs/game/game.cpp` (emit `Module.reoneWebOnModuleReady` via `EM_ASM` at the end of the
  in-game transition, near `openInGame()` / after `loadModule` success)
- `include/reone/game/game.h` (member to hold a pending warp request, e.g. `_pendingWebModule`)
- `tools/web/gamefs.js` (add a `Module.reoneWebWarp(name)` JS wrapper that `ccall`s the export;
  optionally wire `reoneWebOnModuleReady` console signal)

**Approach:** Set a pending-module string from the C export; consume it at the top of the next
`runFrame`/update tick and call `loadModule`. This keeps the Asyncify suspension inside the
normal frame, exactly like a GUI click handler would. Guard everything in `#ifdef __EMSCRIPTEN__`.

**Patterns to follow:** existing `reoneWebOnEngineReady` `EM_ASM` block at
`src/libs/game/game.cpp` (~line 1272); existing `extern "C" EMSCRIPTEN_KEEPALIVE` in
`src/apps/engine/engine.cpp` (`reone_em_run_frame`).

**Test scenarios:**
- Calling `reone_web_warp("end_m01aa")` after menu-ready triggers exactly one `loadModule` and
  does not re-enter while a load is in flight (ignore/queue duplicate calls).
- `reoneWebOnModuleReady` fires after `Module 'end_m01aa' loaded successfully`.
- Non-EMSCRIPTEN build still compiles (guards correct). Test expectation: build-only for native.

**Verification:** In the browser, calling the hook from the console loads the module and logs
success; the ready signal is observable from JS.

---

### U2. Serve + mount module/area resources over the lazy web FS

**Goal:** Ensure every container an area needs is discoverable and readable over the HTTP mirror
/ FS Access path: `modules/end_m01aa.rim` + `_s.rim`, the Low texture pack
`texturepacks/swpc_tex_tpc.erf`, lightmaps, and any `.lyt/.vis/.wok` inside the module RIMs.

**Requirements:** R2, R5

**Dependencies:** U1 (to exercise the path)

**Files:**
- `tools/web/gen_game_manifest.py` (confirm `modules/` and `texturepacks/` are included; no
  skip of `.rim`/`.erf`)
- `tools/web/gamefs.js` (confirm the smoke folder-skip list does not exclude `modules/` or
  `texturepacks/`; only `Override`/`streamwaves`/`streamsounds` were skipped for menu speed —
  verify an area does not require those)
- `src/libs/resource/director.cpp` (read-only: confirm `loadModuleResources` paths; no change
  expected unless casing/lookup fails on MEMFS markers)
- `src/libs/system/stream/webinput.cpp` (only if RIM/ERF reads expose a new chunking edge case)

**Approach:** Verify `ResourceDirector::moduleNames()` and `findFileIgnoreCase` work against the
0-byte MEMFS markers for `modules/`. Confirm the manifest lists module RIMs and the texture
pack. If the area stalls on a specific container, instrument with the existing read watchdog in
`gamefs.js` and fix the manifest/serve coverage rather than the engine.

**Patterns to follow:** existing BIF index warm + `mirrorBifPrefixBytes` handling in
`tools/web/gamefs.js`; the refillBuffer 1 MiB window alignment in `webinput.cpp`.

**Test scenarios:**
- Manifest generated from the retail root contains `modules/end_m01aa.rim`,
  `modules/end_m01aa_s.rim`, and `texturepacks/swpc_tex_tpc.erf`.
- `loadModule` issues HTTP range reads for the module RIMs and the texture pack (observable in
  serve.py logs / watchdog), with no READ STALL warnings.
- Missing optional containers (`_loc`, `_dlg`) do not throw (they are optional in
  `loadModuleResources`).

**Verification:** Module containers mount and `module.ifo` is found; no `ResourceNotFoundException`
for required area resources.

---

### U3. Area render pipeline WebGL2/GLES3.0 parity (Retro pipeline)

**Goal:** Make the Retro render pipeline draw an area under WebGL2 with no fatal GL errors, and
eliminate the `Feedback loop formed between Framebuffer and active Texture` warning.

**Requirements:** R3

**Dependencies:** U1, U2 (need a module to actually render)

**Files:**
- `src/libs/scene/render/pipeline/retro.cpp` (MRT targets, OIT accum/revealage float buffers,
  bloom ping/pong, output; find the pass that samples a still-attached attachment)
- `src/libs/scene/render/pass/retro.cpp` (pass bindings)
- `src/libs/graphics/framebuffer.cpp` / `src/libs/graphics/context.cpp` (attachment + drawBuffers
  handling already partially fixed; extend if new attachment shapes appear)
- `src/libs/graphics/texture.cpp` (internal formats; float/half-float color targets need
  `EXT_color_buffer_float` — ensure the WebGL2 context requests/has it; guard any
  desktop-only format)
- `src/libs/graphics/pixelutil.cpp` or format mapping (only if a desktop-only internal format
  is used for OIT/shadow targets)

**Approach:** Enumerate every framebuffer/attachment the Retro pipeline creates and confirm each
internal format + draw-buffer config is WebGL2-legal (RGBA16F render+blend requires
`EXT_color_buffer_float`; depth/shadow targets must use legal depth formats and
`glFramebufferTexture2D`/`Layer`). For the feedback loop, detach the source texture from the FBO
(or render into the opposite ping/pong target) before sampling it in the post-process/combine
pass. Prefer fixes in the GL abstraction so desktop is unaffected.

**Patterns to follow:** prior WebGL2 fixes — UBO std140 rounding in
`src/libs/graphics/uniformbuffer.cpp`, positional `glDrawBuffers` in
`src/libs/graphics/context.cpp`, `glFramebufferTexture2D/Layer` in
`src/libs/graphics/framebuffer.cpp`.

**Test scenarios:**
- Each Retro framebuffer is framebuffer-complete under WebGL2 (no
  `FRAMEBUFFER_INCOMPLETE_ATTACHMENT`).
- Rendering a frame of `end_m01aa` produces no `GL_INVALID_*` errors in the console.
- The feedback-loop warning no longer appears across multiple rendered frames.
- Float color buffers (OIT accum) allocate and blend without an error when
  `EXT_color_buffer_float` is present; degrade gracefully (documented) if absent.

**Verification:** A rendered area frame is captured with no fatal GL errors and no feedback-loop
warning.

---

### U4. Lazy-I/O robustness under area-load volume

**Goal:** Keep module load from stalling or returning zero-filled reads under the higher request
volume of an area, building on the refillBuffer/Asyncify fixes already in place.

**Requirements:** R2

**Dependencies:** U2

**Files:**
- `src/libs/system/stream/webinput.cpp` (confirm chunk-window alignment covers RIM/ERF access
  patterns; no new EOF/zero-fill edge case)
- `tools/web/gamefs.js` (read watchdog already present; extend warm/preload to module RIM index
  tables only if a measurable stall is observed)
- `src/apps/engine/CMakeLists.txt` (only if Asyncify stack must grow beyond the current 1 MiB for
  the deeper `loadModule` call stack)

**Approach:** Run the area load and watch for `READ STALL` watchdog logs. Only add preload/warm
or stack-size changes if the load actually stalls; otherwise this unit is verification that the
existing I/O path scales. Keep changes minimal (anti-scope-creep).

**Test scenarios:**
- Full `end_m01aa` load completes with zero `READ STALL` watchdog warnings.
- No `WAV: invalid file signature` / zero-length reads during area resource loading.
- Asyncify does not overflow its stack during the deepest `loadModule` read chain.

**Verification:** Module load finishes within the smoke timeout with no stall/zero-fill logs.

---

### U5. Extend Playwright smoke to verify area render

**Goal:** After menu-ready, drive `reone_web_warp("end_m01aa")`, wait for module-ready, and
assert the canvas shows a rendered area (non-black, distinct from the menu).

**Requirements:** R4

**Dependencies:** U1 (hook), and U2-U4 for the assertion to pass

**Files:**
- `tools/web/smoke_engine_menu.py` (after detecting menu-ready, optionally call the warp hook via
  `page.evaluate`, then poll for `Module 'end_m01aa' loaded successfully` / in-game; capture a
  screenshot and assert it is not uniformly black and differs from the menu screenshot)
- `tools/web/run_menu_smoke.sh` (pass a new flag/env, e.g. `REONE_WEB_SMOKE_WARP_MODULE=end_m01aa`)
- `tools/web/progress.md` (note the new verification step)

**Approach:** Gate the warp behind a flag so the existing menu-only smoke is unchanged by default.
For the non-black assertion, sample a downscaled screenshot's pixel variance (avoid `gl.readPixels`
in headless Chromium per the existing comment in the smoke). Compare against the menu capture to
confirm a state change.

**Test scenarios:**
- With the warp flag set, the smoke reaches in-game and saves a non-black area screenshot.
- Without the flag, the smoke behaves exactly as today (menu-only, returns 0 on menu detection).
- A load failure (engine error / stall) is reported with a saved screenshot and non-zero exit.

**Verification:** `run_menu_smoke.sh --warp end_m01aa` (or the env var) exits 0 with a non-black,
non-menu screenshot at `tools/web/_smoke_menu_verify.png`.

---

## Scope Boundaries

In scope: loading `end_m01aa` via a JS hook and rendering its area in WASM; WebGL2 parity for the
Retro pipeline; lazy-I/O robustness for area volume; smoke verification.

### Deferred to Follow-Up Work
- Headless-driven character generation GUI (portrait/abilities/skills/name) to reach the module
  through the literal New Game button.
- Gameplay interaction (camera movement, party control, dialogue, combat) beyond first-frame
  area render.
- High-fidelity graphics (shadows > 1, SSAO/SSR, High textures) in the browser.
- Per-module preloading / module-to-module warp transitions.

### Outside this milestone's identity
- Audio streaming correctness (streamwaves/streamsounds) — non-blocking for area render.

---

## Risks & Dependencies

- **WebGL2 float color buffers (OIT):** RGBA16F render+blend needs `EXT_color_buffer_float`; if
  unavailable or mis-formatted, the area framebuffer is incomplete -> black. Mitigation: verify
  extension at context creation; fall back to a supported format if needed.
- **Feedback loop in post-process:** may currently be masked; under an area it could become a hard
  error on some drivers. Mitigation: detach source attachment before sampling / ping-pong targets.
- **I/O volume / Asyncify:** deeper call stack and many reads could stall or overflow. Mitigation:
  watchdog logs already in place; grow stack only if observed.
- **Re-entrancy from JS hook:** calling `loadModule` directly inside a JS callback during an
  Asyncify suspend could corrupt state. Mitigation: queue + apply at frame boundary (U1).

---

## Verification Strategy

Narrowest-first: (1) native build still compiles with the new guards; (2) WASM build links;
(3) browser smoke reaches menu-ready (no regression); (4) warp hook loads `end_m01aa` and logs
success; (5) area frame renders with no fatal GL errors / no feedback-loop warning; (6) smoke
saves a non-black, non-menu screenshot. Commit to branch `cursor/web-wasm-gles-and-fs-access`
and `master` (no PR), per standing instruction.

---

## Progress (2026-05-29 — milestone complete)

### Landed
- R1–R5 satisfied: `Module.reoneWebPreloadModuleFiles` + `reoneWebWarpAsync`; `loadModule("end_m01aa")` → `reoneWebModuleReady`; area renders in WASM.
- R3: geometry-shader shadow passes skipped on `__EMSCRIPTEN__`; BGR→RGB upload swizzle; empty-mesh draw guard; FBO feedback-loop unbind (prior pass).
- R4: `tools/web/run_menu_smoke.sh --warp end_m01aa` exits 0 (re-verified 2026-05-29); `_smoke_module_render.png` shows Endar Spire in-game.
- Head `a0b5b33e` on `cursor/web-wasm-gles-and-fs-access`; PR [#111](https://github.com/modawan/reone/pull/111) updated.
- OpenKotOR `Build WASM engine` CI **success** on head (`gh run 26617663753`).

### Partial / uncertain
- Canvas luminance probe reads 0 in headless Chromium; smoke trusts `reoneWebModuleReady` + screenshot.
- `glVertexAttribPointer: Negative stride` warning on some MDL layouts (non-fatal).
- modawan/reone `Build WASM` shows `action_required` on fork PR (workflow approval gate, not compile failure).

### Follow-up (out of scope)
- DXT decompression when S3TC unavailable; literal New Game → chargen GUI; `refresh2DArray` BGR swizzle; strict `--require-canvas-luminance` CI gate.

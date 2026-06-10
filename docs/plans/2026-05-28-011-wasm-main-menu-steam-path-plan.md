---
title: "feat: WASM main menu with Steam KotOR install"
type: feat
status: completed
date: 2026-05-28
superseded_by: docs/plans/2026-05-28-012-feat-wasm-newgame-module-render-plan.md
---

# feat: WASM main menu with Steam KotOR install

> **Status: COMPLETED.** The main menu renders in the browser WASM build from a retail Steam
> install (committed to `cursor/web-wasm-gles-and-fs-access` + `master`). The active milestone
> continues in `docs/plans/2026-05-28-012-feat-wasm-newgame-module-render-plan.md` (New Game ->
> load first module -> render area).

## Summary

Reach **main menu** using `/run/media/brunner56/MyBook/SteamLibrary/steamapps/common/swkotor` via `serve.py --game-root`. Match native `ResourceDirector` boot: preload init archives, then run engine. **chitin.key** gates startup; **swkotor.exe optional**.

## Requirements

- R1. Game root: `chitin.key` + `dialog.tlk` TLK V3.0; exe not required for discovery or `hasMountedGameData`.
- R2. Do not publish **partial** BIF slices to MEMFS (breaks sync reads); only full files or lazy WebFileInputStream.
- R3. `GameProbe` falls back to `chitin.key` when exe absent.
- R4. `smoke_engine_menu.py` exit 0 with retail path; fix asyncify/runtime errors until menu.
- R5. Push on `cursor/web-wasm-gles-and-fs-access` + `origin/master` (no PRs).

## Test

```bash
export REONE_WEB_SMOKE_GAME_ROOT="/run/media/brunner56/MyBook/SteamLibrary/steamapps/common/swkotor"
./tools/web/run_menu_smoke.sh --no-nuke-stale-servers
```

## Delta Update (2026-05-28) — MAIN MENU RENDERS ✅

Browser WASM smoke now boots the retail Steam install to the **KotOR main menu**
(STAR WARS / KNIGHTS OF THE OLD REPUBLIC logo + NEW GAME / LOAD GAME / MOVIES /
OPTIONS / QUIT). `Main menu ready` at ~7 s; screenshot in `tools/web/_smoke_menu_verify.png`.

- **Landed (verified in browser):**
  - `gameprobe.cpp` falls back to `chitin.key` -> KotOR when no exe (R1, R3). No `.exe` is a hard gate.
  - BIF index tables for huge archives preloaded as JS Range slices and read synchronously by
    `KeyBifResourceContainer::init` via `Module.reoneWebGetBifIndexBytes` (no preRun `_malloc`,
    no Asyncify) -> fixed `EOF reading BIF: models.bif`.
  - **I/O read bug fixed** (`webinput.cpp::refillBuffer`): buffer window now aligns to the full 1 MiB
    `kWebMirrorReadChunk` while each async fetch stays capped at 256 KiB. Previously any `pos` >256 KiB
    past a 1 MiB boundary missed the window and read zeros -> corrupt WAV signature + stalled `models.bif`
    read. This was the root cause of the black-screen stall.
  - Audio load made non-fatal (`audioclips.cpp` try/catch) so a bad clip can't black-screen the menu.
  - **GLAD** patched to enable GL 3.1-3.3 entry points under a WebGL2 "OpenGL ES 3.0" version string
    (UBO functions were NULL -> `null function` in `bindUniformBlock`).
  - **WebGL2 graphics parity:** `glFramebufferTexture` -> `glFramebufferTexture2D`/`glFramebufferTextureLayer`;
    `glDrawBuffer(GL_NONE)` -> `glDrawBuffers`; `GL_CLAMP_TO_BORDER`/border color and `glGetTexImage`
    guarded out on Emscripten.
  - **UBO too-small fix** (`uniformbuffer.cpp`): allocate the GL buffer rounded up to a std140 16-byte
    multiple (upload only real bytes). Desktop GL tolerated the undersized buffer; WebGL2 rejected every
    `glDrawElements` -> black screen. This made the menu actually draw.
  - **drawBuffers positional fix** (`context.cpp`): build a positional `glDrawBuffers` array (slot i =
    `COLOR_ATTACHMENTi` or `GL_NONE`) so single attachments at index >0 are valid in WebGL2.
- **Partial / uncertain:**
  - `GL_INVALID_OPERATION: glDrawElements: Feedback loop formed between Framebuffer and active Texture`
    spams (~256/frame) from a post-process pass that samples a texture still attached to the bound FBO.
    **Non-fatal** — menu renders correctly. Should detach/clear that sampler binding before the pass.
  - Heavy folders (`streamwaves`, `Override`, `streamsounds`) skipped in the HTTP mirror for smoke speed.
- **Next:**
  - Commit + push to `cursor/web-wasm-gles-and-fs-access` + `origin/master`, no PR (R5).
  - Optional: silence the feedback-loop warning; broaden smoke to in-game once menu is stable.

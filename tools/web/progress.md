Original prompt: ReOne WASM playability and openkotor-site integration — `gamefs.js` (FS Access via `--pre-js`), `serve.py`, optional manifest tooling for HTTP mirrors, CI WASM build, openkotor-site `/play/*` routes, `vercel.json` COOP/COEP, docs.

## Build verification (2026-05-27)

- Local **Release** wasm build succeeded with emsdk **5.0.7**: `build-web/bin/engine.{html,js,wasm,data}` (~7.5 MB wasm) passes `verify_wasm_bundle.py`.
- `test_serve_smoke.py` passes.
- **Main menu smoke (retail install required):**
  ```bash
  export REONE_WEB_SMOKE_GAME_ROOT="/path/to/KotOR"   # chitin.key + dialog.tlk (swkotor.exe optional)
  ./tools/web/run_menu_smoke.sh
  ```
  Uses `tools/web/.venv` (Playwright). GemRB demo data is **not** a valid KotOR install (KEY/BIF mismatch).
- Lazy web I/O: `WebFileInputStream` defers HTTP stat until first read (Asyncify-safe).
- **CI parity script:** `tools/web/ci_build_wasm.sh` (same steps as `.github/workflows/build-wasm.yml`).
- **CI (2026-06-03):** `wasm-ci` runs on **`ubuntu-latest`** (GitHub-hosted). Self-hosted `reone-wasm-BodenPC` was retired after checkout auth failures; see PR #7 wasm-ci fixes. **`cancel-in-progress: false`** — wasm links take ~30–90 min; do not abort mid-compile on PR churn.
- **Tracking:** [OpenKotOR/reone#3](https://github.com/OpenKotOR/reone/issues/3) closed with resolution link to first green run [26556275588](https://github.com/OpenKotOR/reone/actions/runs/26556275588).
- **`master` push (2026-06-03):** `build-wasm.yml` now triggers on **`master`** (not only `cursor/**`) so post-GLES merges keep wasm-ci green.
- **serve.py (fixed):** CI integration smoke uses `--directory /tmp/web-empty --game-root …`; serve now allows **game-mirror-only** mode when `engine.html` is absent but `--game-root` is set.

## PR status (2026-06-10 — LFG pass 16)

- **[OpenKotOR/reone#24](https://github.com/OpenKotOR/reone/pull/24)** OPEN — re-sync `cursor/web-wasm-gles-and-fs-access` with `master` after #4 squash drift (`cce4ae50`).
- Cancelled stuck `wasm-ci` run `27273886615` (queued 4h+); dispatched [27290837408](https://github.com/OpenKotOR/reone/actions/runs/27290837408) @ `cce4ae50`.
- Local: `Installation*` gtests **6/6 pass** on `build-gles-test`.

## PR status (2026-06-04)

- **[OpenKotOR/reone#4](https://github.com/OpenKotOR/reone/pull/4)** merged — WASM menu, module warp, `extract::Installation` port (`68d7f3ad`).
- **[OpenKotOR/reone#5](https://github.com/OpenKotOR/reone/pull/5)** merged — dataminer → `Installation` (`e8e4b678`).
- **[OpenKotOR/reone#7](https://github.com/OpenKotOR/reone/pull/7)** merged — OpenGL ES 3.0 engine + GLES CI smokes (`20e32664`).
- **[OpenKotOR/reone#10](https://github.com/OpenKotOR/reone/pull/10)** merged — wasm-ci on `master` pushes.
- **[OpenKotOR/reone#11](https://github.com/OpenKotOR/reone/pull/11)** merged — save-game peek via `ErfReader`; legacy containers removed.
- **[OpenKotOR/reone#14](https://github.com/OpenKotOR/reone/pull/14)** merged — DialogGUI talk position guard (`fdd9bf84`).
- **[OpenKotOR/reone#15](https://github.com/OpenKotOR/reone/pull/15)** merged — GLES black model/texture fix on real GPU (`d9aff290`); smokes use `build-gles/bin`, not stale `debug/bin`.
- **[OpenKotOR/reone#18](https://github.com/OpenKotOR/reone/pull/18)** merged — modawan fork integration (`23a518fe`).
- **[OpenKotOR/reone#22](https://github.com/OpenKotOR/reone/pull/22)** merged — modawan #167 handoff @ `e33f322a` (`4c669589`).
- **`master`** @ `4c669589`; Linux/Windows/wasm-ci/gles-linux green.
- **Downstream:** [modawan/reone#167](https://github.com/modawan/reone/pull/167) supersedes closed #163 — `OpenKotOR:glad-gles` @ `e33f322a`, **MERGEABLE**; maintainer squash-merge required.
- [modawan/reone#163](https://github.com/modawan/reone/pull/163) — closed without merge (2026-06-05); use #167.
- [modawan/reone#166](https://github.com/modawan/reone/pull/166) — closed; integrate via #167.

## Done (implementation)

- [`serve.py`](serve.py) (2026-05-03): port **4204**, `--game-root`, Range lazy files; default bind **127.0.0.1**; **`allow_reuse_address=False` on Windows** to avoid port-sharing empty responses (`net::ERR_EMPTY_RESPONSE`); optional **`--host ::`** for dual-stack.
- [`smoke_engine_menu.py`](smoke_engine_menu.py): **HTTP preflight** fails fast (exit **3**) when `/game-manifest.json` is 503 (no `--game-root`).
- [`gamefs.js`](gamefs.js) is linked with Emscripten `--pre-js`.
- [`gen_game_manifest.py`](gen_game_manifest.py) emits `game-manifest.json`.
- [`test_serve_smoke.py`](test_serve_smoke.py) verifies manifest + Range; **`.github/workflows/build-wasm.yml`** runs it plus a live `gen_game_manifest` + `serve.py` integration step, then builds `engine` with Emscripten and uploads `engine.{html,js,wasm,data}`.

### KotOR.js vs ReOne (why a “1 GB .wasm” is not KotOR.js)

- **KobaltBlu/KotOR.js** is a **JavaScript** engine (Webpack → `dist/`), uses **`wicg-file-system-access`** types in devDeps — game data is **not** in the repo or in a giant WASM blob; the browser reads the user’s install via **File System Access** (same model as ReOne’s default `gamefs.js`).
- A **multi‑GB** artifact with ReOne is almost always **`engine.data`** from **`--preload-file`** / **`file_packager`** after **`REONE_WEB_ALLOW_EMBEDDED_GAME_BUNDLE=ON`** + full install — **not** normal parity builds. Default CMake is **`REONE_WEB_ASSET_PROFILE=none`** (no baked `/game`).

## Manual acceptance (main menu)

Requires locally:

1. Emscripten + Ninja + Boost headers; configure `build-web` with `REONE_WEB_GAME_DIR` **unset** (lazy mode).
2. KotOR install on disk; run `python tools/web/gen_game_manifest.py "C:/Path/To/KotOR" --strict`.
3. `python tools/web/serve.py --directory build-web/bin --game-root "C:/Path/To/KotOR"`.
4. Open `http://localhost:4204/engine.html` — expect main menu; verify Network loads `/game-manifest.json` and ranged `/game-files/...`.

**Fallback:** embedded preload only with explicit `-DREONE_WEB_ALLOW_EMBEDDED_GAME_BUNDLE=ON` plus `-DREONE_WEB_GAME_DIR=...` (large `engine.data`).

## TODO / suggestions

- After first successful menu capture, add screenshot to PR / docs.
- If pthread WASM is enabled, confirm COOP/COEP on production host (see openkotor-site `vercel.json`).
- If **`build-wasm.yml`** fails after an Emscripten upgrade, pin **`version:`** under **`emscripten-core/setup-emsdk`** (see [setup-emsdk README](https://github.com/emscripten-core/setup-emsdk)).

## develop-web-game workflow (OpenKotOR site)

- **Client:** Vendored Playwright runner + payloads live in **openkotor-site** — `scripts/web-game-playwright.mjs`, `scripts/web-game-action_payloads.json` (Apache-2.0, from Codex develop-web-game skill).
- **npm:** `playwright` devDependency; scripts `test:develop-web-game:play-reone` and `test:develop-web-game:kotor-js` target `http://127.0.0.1:5173/...` (run `npm run dev -- --host 127.0.0.1 --port 5173 --strictPort` if port 3000 is unavailable).
- **2026-05-02 run:** Both tests exited 0. Screenshots under `openkotor-site/output/web-game-play-*` (gitignored): `/play/reone` shows hub chrome + black iframe (expected when `localhost:4204` engine is down or X-Frame blocks); `/play/kotor-js` iframe showed live KotOR.js video UI.
- **Limits:** `render_game_to_text` / deterministic `advanceTime` are **not** wired for Emscripten/reone (would need JS glue on `engine.html`). Parent-page canvas detection **does not** see WebGL inside a **cross-origin** iframe; full WASM verification stays manual on `http://localhost:4204/engine.html` or future Playwright `frameLocator` work.

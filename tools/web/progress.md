Original prompt: ReOne WASM playability and openkotor-site integration — `gamefs.js` (FS Access via `--pre-js`), `serve.py`, optional manifest tooling for HTTP mirrors, CI WASM build, openkotor-site `/play/*` routes, `vercel.json` COOP/COEP, docs.

## Build verification (2026-05-27)

- Local **Release** wasm build succeeded with emsdk **5.0.7**: `build-web/bin/engine.{html,js,wasm,data}` (~7.5 MB wasm) passes `verify_wasm_bundle.py`.
- `test_serve_smoke.py` passes.
- Menu Playwright smoke needs `pip install playwright && python -m playwright install chromium` plus a real KotOR install (`--game-root`).
- **CI parity script:** `tools/web/ci_build_wasm.sh` (same steps as `.github/workflows/build-wasm.yml`).
- **CI queue (2026-05-28):** OpenKotOR `Build WASM` runs may sit in `queued` when GitHub-hosted runner pools are saturated; local `ci_build_wasm.sh` is the authoritative green path until Actions assigns a runner.

## PR status (2026-05-27)

- [modawan/reone#111](https://github.com/modawan/reone/pull/111) is **OPEN (draft)** — it was **not** merged on GitHub.
- A prior local-only `git merge upstream/master` was **reverted**; branch tip is again `origin/cursor/web-wasm-gles-and-fs-access`.
- CI for fork PRs runs on **OpenKotOR/reone** when the head branch is pushed; `modawan/reone` may show “no checks” for cross-repo PRs until workflows are approved on the fork.

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

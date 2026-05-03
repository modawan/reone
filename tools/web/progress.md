Original prompt: ReOne WASM playability and openkotor-site integration — `gamefs.js` (FS Access via `--pre-js`), `serve.py`, optional manifest tooling for HTTP mirrors, CI WASM build, openkotor-site `/play/*` routes, `vercel.json` COOP/COEP, docs.

## Done (implementation)

- [`gamefs.js`](gamefs.js) is linked with Emscripten `--pre-js`.
- [`serve.py`](serve.py): default port **4204**, `--game-root`, Range requests for lazy files.
- [`gen_game_manifest.py`](gen_game_manifest.py) emits `game-manifest.json`.
- [`test_serve_smoke.py`](test_serve_smoke.py) verifies manifest + Range (runs in CI).

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
- Consider deleting `test_serve_smoke.py` from developer flows if a fuller integration test is added later.

## develop-web-game workflow (OpenKotOR site)

- **Client:** Vendored Playwright runner + payloads live in **openkotor-site** — `scripts/web-game-playwright.mjs`, `scripts/web-game-action_payloads.json` (Apache-2.0, from Codex develop-web-game skill).
- **npm:** `playwright` devDependency; scripts `test:develop-web-game:play-reone` and `test:develop-web-game:kotor-js` target `http://127.0.0.1:5173/...` (run `npm run dev -- --host 127.0.0.1 --port 5173 --strictPort` if port 3000 is unavailable).
- **2026-05-02 run:** Both tests exited 0. Screenshots under `openkotor-site/output/web-game-play-*` (gitignored): `/play/reone` shows hub chrome + black iframe (expected when `localhost:4204` engine is down or X-Frame blocks); `/play/kotor-js` iframe showed live KotOR.js video UI.
- **Limits:** `render_game_to_text` / deterministic `advanceTime` are **not** wired for Emscripten/reone (would need JS glue on `engine.html`). Parent-page canvas detection **does not** see WebGL inside a **cross-origin** iframe; full WASM verification stays manual on `http://localhost:4204/engine.html` or future Playwright `frameLocator` work.

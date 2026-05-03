# WebAssembly Browser Build

## Main theme (read first)

no shipping or embedding the retail install in the web artifact; **on-demand** access via **File System Access** (`showDirectoryPicker`, directory/file handles, IndexedDB persistence). Emscripten `**file_packager` must not bake `/game`** unless you explicitly opt in with `**REONE_WEB_ALLOW_EMBEDDED_GAME_BUNDLE=ON**` (non-parity, huge `engine.data`, browser-hostile). Default CMake is `**REONE_WEB_ASSET_PROFILE=none**` and `**REONE_WEB_GAME_DIR` unset**.

---

This guide covers building and running `reone` in a browser with WebAssembly (Emscripten + SDL3).

The browser target disables native-only tools, omits movie/MP3 codecs by default on web, and focuses on bringing up `engine` with WebGL2.

**Design priority:** same as above game directory handling (File System Access, no baked install by default). ReOne’s WASM/JS glue follows that model first; embedded preload is **opt-in only** (`REONE_WEB_ALLOW_EMBEDDED_GAME_BUNDLE`).

## What Gets Built

- Target: `engine`
- Output directory: `build-web/bin`
- Artifacts:
  - `engine.html`
  - `engine.js` (includes `tools/web/gamefs.js` via Emscripten `**--pre-js`**, same pattern class as typical
  - `engine.wasm`
  - Optional `engine.data` when using **embedded preload** (CI/smoke or special cases only)

### `build-web-docker` vs `build-web` on Windows

The Docker workflow (`Dockerfile.emscripten`, build dir `build-web-docker`) configures Ninja with **Linux paths** (for example `/usr/bin/cmake`). Treat **`build-web-docker` as Docker-only**: do not run `ninja` there from native Windows — it will fail trying to invoke those paths. On Windows, configure a fresh tree with **`emcmake … -B build-web`** (or another empty directory) and build there, **or** run the Docker command from the Dockerfile and copy **`engine.html` / `engine.js` / `engine.wasm`** out of the container output; do not expect an in-place rebuild of `build-web-docker` without Docker.

## Prerequisites

### Common

- CMake (3.16+)
- Python 3 (`tools/web/serve.py`, optional `tools/web/gen_game_manifest.py` for local mirrors)
- Emscripten SDK (`emsdk`)
- Ninja
- Boost headers available to CMake (`find_package(Boost)` — web build does not need `program_options` linked)

### Windows (example)

```powershell
winget install -e --id Ninja-build.Ninja --accept-source-agreements --accept-package-agreements

Set-Location C:\GitHub
git clone https://github.com/emscripten-core/emsdk.git
Set-Location C:\GitHub\emsdk
.\emsdk install latest
.\emsdk activate latest

# Boost headers (vcpkg example)
C:\path\to\vcpkg\vcpkg.exe install boost-core --triplet x64-windows
```

### macOS

```bash
brew install cmake ninja python boost
# emsdk clone + ./emsdk install latest && source ./emsdk_env.sh
```

### Linux (Debian/Ubuntu)

```bash
sudo apt update
sudo apt install -y cmake ninja-build python3 git build-essential libboost-all-dev
# emsdk clone + ./emsdk install latest && source ./emsdk_env.sh
```

## Configure

From the repository root:

```bash
emcmake cmake -G Ninja -S . -B build-web -DREONE_PLATFORM_WEB=ON -DCMAKE_BUILD_TYPE=Release
```

### Windows (if `emcmake` is not on PATH)

```powershell
C:\path\to\emsdk\upstream\emscripten\emcmake.bat cmake -G Ninja -S . -B build-web -DREONE_PLATFORM_WEB=ON -DCMAKE_BUILD_TYPE=Release -DBoost_INCLUDE_DIR=C:/path/to/boost/include
```

## Game files: pick exactly one runtime mode

There are **two** supported ways to supply a KotOR install to the WASM engine. They are **mutually exclusive** for mental models: either the **browser** reads your disk via File System Access, or **`serve.py`** exposes an HTTP mirror for tooling. Mixing them “just because” creates confusing DevTools noise.

### Mode A — File System Access (default / retail parity)

- **What:** User selects the install folder in the browser (`showDirectoryPicker`). Same idea as KotOR.js.
- **Serve:** Static WASM only — **do not pass** `--game-root` to `serve.py`.
- **URL:** Use `**/engine.html?reoneFs=picker**` so `gamefs.js` **never** probes `/game-manifest.json` (no fake “failed to load manifest” red herrings when no mirror exists).
  - Equivalent JS flag (advanced): `Module.reoneWebFsDisableHttpMirror = true` before `engine.js`.
- **Pick:** Choose the directory that contains **`chitin.key`** (e.g. Steam `…\steamapps\common\swkotor`).
- **Persistence:** Last-used handle can be stored in **IndexedDB** (same origin).

### Mode B — HTTP lazy mirror (CI / automation only)

- **What:** `serve.py --game-root /path/to/KotOR` serves `game-manifest.json` + ranged `/game-files/…`; no folder picker.
- **Prepare:** `python tools/web/gen_game_manifest.py "/path/to/KotOR"` writes `game-manifest.json` next to the install.
- **Do not use** `?reoneFs=picker` here — you want the mirror probe to succeed.

### Environment variables (Web FS smoke tests only)

| Variable | Used by | Meaning |
|----------|---------|--------|
| **`REONE_WEB_SMOKE_GAME_ROOT`** | `tools/web/smoke_engine_menu.py` when `--game-root` omitted | Absolute KotOR path **only** for **`--spawn-serve`** HTTP mirror automation. **Not** read by `serve.py`, CMake, or `gamefs.js`. |
| **`PLAYWRIGHT_HEADED`** | smoke script | Set `1` / `true` if you need a visible browser window. |

There is **no** repository-wide env var that replaces the folder picker for Mode A.

---

## File System Access API (detail)

Requirements:

- **Chromium-class browser** (`showDirectoryPicker`): Chrome, Edge, etc.
- **Secure context** (HTTPS or `http://localhost`)
- User grants **read/write** on the folder for browser builds

Optional: before loading `engine.js`, set `Module.reoneGameDirectoryHandle` to an existing `FileSystemDirectoryHandle` (advanced/same-origin shells only). Otherwise the overlay in `gamefs.js` collects the folder after load.

After you pick the install folder, `gamefs.js` indexes file handles into lightweight Emscripten `**MEMFS`** marker paths under `**/game`**. Actual archive/data bytes are **not copied up front**: web builds read exact byte ranges on demand through `WebFileInputStream` using `FileSystemFileHandle.getFile()` + `Blob.slice(...)` (or HTTP `Range` in Mode B only). Paths are stored **lowercased** so lookups match the engine’s case-insensitive layout.

- **RAM**: startup does not mirror the full install into browser memory; memory scales with decoded resources and requested archive slices.
- **Skips (default)**: top-level `**movies/`**, `**streammusic/`**, and `**saves/**` are not indexed (saves startup work; usually enough to reach the main menu). For a full tree, set `**Module.reoneWebFsNoSkip = true**` before `engine.js`, or set `**Module.reoneWebFsSkipTopDirs = []**` (empty array: no directory skips). `**reoneWebFsSkipTopDirs**` can also list lower-case top-level folder names to skip; omit or leave unset to use the default list above (ignored when `**reoneWebFsNoSkip**` is true).

### Embedded preload (optional)

Whether to embed the install into the build. ReOne matches that by default (`REONE_WEB_ASSET_PROFILE=none`, `REONE_WEB_GAME_DIR` unset).

**CMake will refuse `REONE_WEB_GAME_DIR` unless you also pass `-DREONE_WEB_ALLOW_EMBEDDED_GAME_BUNDLE=ON`.** That keeps accidental multi‑GB `file_packager` runs out of normal developer flows.

To bake an entire install into `engine.data` (large artifact; debugging or browsers without File System Access):

```bash
emcmake cmake -G Ninja -S . -B build-web \
  -DREONE_PLATFORM_WEB=ON \
  -DCMAKE_BUILD_TYPE=Release \
  -DREONE_WEB_ALLOW_EMBEDDED_GAME_BUNDLE=ON \
  -DREONE_WEB_GAME_DIR=/absolute/path/to/KotOR \
  -DREONE_WEB_ASSET_PROFILE=full
```

Custom subsets: same `**REONE_WEB_ALLOW_EMBEDDED_GAME_BUNDLE=ON**` as above, then `-DREONE_WEB_ASSET_PROFILE=custom` plus `-DREONE_WEB_PRELOAD_ITEMS='rel/path/a;rel/path/b'` (CMake list, semicolon-separated).

Default builds embed **no** game files, so `/game` is empty until you pick a folder.

If you use CMake `**full`** / `**custom`** embed, set `**Module.reoneAssumeEmbeddedGamePreload = true`** before `engine.js` so the folder picker is skipped (non-parity). Otherwise `gamefs.js` still tries File System Access and mounting may conflict with existing nodes.

### Experimental: pthreads
** ReOne keeps `**REONE_WEB_PTHREADS` OFF** by default. Turning it on is **experimental** (different perf/debug surface, COOP/COEP requirements).

```bash
emcmake cmake -G Ninja -S . -B build-web-pthreads \
  -DREONE_PLATFORM_WEB=ON \
  -DREONE_WEB_PTHREADS=ON \
  -DCMAKE_BUILD_TYPE=Release
```

Pthreads require **COOP/COEP** headers at runtime (the included `serve.py` sets them).

## Build

```bash
cmake --build build-web --target engine -j 8
```

## Serve locally

The helper server sets **WASM MIME**, **COOP / COEP / CORP** headers and serves the build output.

**Mode A — folder picker (no `--game-root`):**

```bash
python tools/web/serve.py --directory build-web/bin --port 4204
```

Open (note query — skips HTTP mirror probe):

```text
http://localhost:4204/engine.html?reoneFs=picker
```

Use **Choose folder…** and select your KotOR install root (must contain `chitin.key`), e.g. Steam:

```text
G:\SteamLibrary\steamapps\common\swkotor
```

### Mode B — HTTP mirror (tooling / `smoke_engine_menu.py --spawn-serve` only)

`serve.py --game-root …` exposes `game-manifest.json` + ranged `/game-files/…`. Playwright smoke tests use **`REONE_WEB_SMOKE_GAME_ROOT`** or `--game-root` — that variable **only** feeds `--spawn-serve`, not the retail picker flow.

Do **not** rely on a plain `python -m http.server` for WASM + experimental pthreads: missing COOP/COEP breaks `SharedArrayBuffer`; missing `.wasm` MIME types breaks instantiation.

## CI

Workflow `.github/workflows/build-wasm.yml` builds `engine` with Emscripten on Ubuntu and uploads `engine.html`, `engine.js`, and `engine.wasm` as artifacts. It does **not** exercise the File System Access picker (non-automatable in default CI). The `**serve-smoke`** job only validates `serve.py` manifest + Range behavior for optional tooling.

## Production hosting and OpenKotOR site

- **GitHub Pages** cannot set custom `Cross-Origin-Opener-Policy` / `Cross-Origin-Embedder-Policy` headers. For pthread experiments or strict embedding, host static `engine.`* on **Vercel**, **Cloudflare Pages**, or another host that supports security headers.
- The OpenKotOR site includes example `**vercel.json`** headers for paths under `/reone/`*. Deploy WASM artifacts into `public/reone/` (or adjust `source` patterns) so those headers apply.
- Play hub routes: `/play/reone` (iframe / env `VITE_REONE_PLAY_URL`) and `/play/kotor-js` (`VITE_KOTORJS_PLAY_URL`, default `https://play.swkotor.net/`). The reone iframe uses the **same-origin** engine URL so the folder picker runs inside the frame.

## Troubleshooting

### `emcmake: no compatible cmake generator found`

Install Ninja and use `-G Ninja`.

### `Could NOT find Boost`

Install Boost development packages or pass `-DBoost_INCLUDE_DIR=...` on Windows.

### `Could not find SDL3Config.cmake`

Web builds use Emscripten’s SDL3 port (`-sUSE_SDL=3`); no system SDL3 is required.

### Page loads but engine exits immediately on web

- **FS Access mode**: Confirm you picked the folder that contains `**chitin.key`**. Use a Chromium-class browser.
- **Full/custom embed**: confirm `REONE_WEB_ASSET_PROFILE` and `/game/chitin.key` after preload.

### Blank canvas / shader errors

Web uses the `glsl` folder preloaded at `/glsl` when `shaderpack.erf` is absent. Confirm `glsl` was packaged into the build output as configured in CMake.

## Current limitations

- No full movie (Bink) parity in web builds (`ENABLE_MOVIE` off).
- No MP3 decode in default web flags (`ENABLE_MP3` off).
- File System Access API is not available in Firefox/Safari today; use **CMake embedded preload** for those browsers if you need a non-picker path.
- Performance tuning and asset UX are ongoing.


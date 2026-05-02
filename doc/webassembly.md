# WebAssembly Browser Build

## Main theme (read first)

**Match [KotOR.js](https://github.com/KobaltBlu/KotOR.js) for browser assets:** no shipping or embedding the retail install in the web artifact; **on-demand** access via **File System Access** (`showDirectoryPicker`, directory/file handles, IndexedDB persistence — see KotOR.js `GameFileSystem.ts`). Emscripten **`file_packager` must not bake `/game`** unless you explicitly opt in with **`REONE_WEB_ALLOW_EMBEDDED_GAME_BUNDLE=ON`** (non-parity, huge `engine.data`, browser-hostile). Default CMake is **`REONE_WEB_ASSET_PROFILE=none`** and **`REONE_WEB_GAME_DIR` unset**. *(KotOR.js itself is TypeScript + THREE.js, not WASM; “parity” here means the same **asset delivery model** for reone’s WASM build.)*

---

This guide covers building and running `reone` in a browser with WebAssembly (Emscripten + SDL3).

The browser target disables native-only tools, omits movie/MP3 codecs by default on web, and focuses on bringing up `engine` with WebGL2.

**Design priority:** same as above — **[KobaltBlu/KotOR.js](https://github.com/KobaltBlu/KotOR.js)**-style game directory handling (File System Access, no baked install by default). ReOne’s WASM/JS glue follows that model first; embedded preload is **opt-in only** (`REONE_WEB_ALLOW_EMBEDDED_GAME_BUNDLE`).

## What Gets Built

- Target: `engine`
- Output directory: `build-web/bin`
- Artifacts:
  - `engine.html`
  - `engine.js` (includes `tools/web/gamefs.js` via Emscripten `**--pre-js`**, same pattern class as typical KotOR.js-style FS glue)
  - `engine.wasm`
  - Optional `engine.data` when using **embedded preload** (non–KotOR.js parity; CI/smoke or special cases only)

## Prerequisites

### Common

- CMake (3.16+)
- Python 3 (`tools/web/serve.py`, optional `tools/web/gen_game_manifest.py` for local mirrors)
- Emscripten SDK (`emsdk`) — **pin or compare versions with KotOR.js** if lazy file behavior differs from theirs on “latest”
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

## KotOR.js alignment (default): File System Access API

Like **KotOR.js**, the normal browser build **does not bake the game into the repo**. At runtime, `gamefs.js` (linked via `**--pre-js`**) waits for a folder the user selects that contains `**chitin.key`** at the install root. The handle can be remembered in **IndexedDB** (same idea as KotOR.js storing launcher-related handles).

Requirements:

- **Chromium-class browser** (`showDirectoryPicker`): Chrome, Edge, etc.
- **Secure context** (HTTPS or `http://localhost`)
- User grants **read/write** on the folder (same permission model KotOR.js uses for browser builds)

Optional: before loading `engine.js`, set `Module.reoneGameDirectoryHandle` to an existing `FileSystemDirectoryHandle` (advanced/same-origin shells only). Otherwise the overlay in `gamefs.js` collects the folder after load.

After you pick the install folder, `gamefs.js` copies files into Emscripten `**MEMFS`** under `**/game`** using `**FS.writeFile`** (modern browsers block main-thread `**FS.createLazyFile**`, unlike older Emscripten/worker setups). Paths are stored **lowercased** so lookups match the engine’s case-insensitive layout.

- **RAM**: a full install can use **several GB** of browser memory. Close other tabs; prefer a 64-bit browser.
- **Skips (default)**: top-level `**movies/`**, `**streammusic/`**, and `**saves/**` are not copied (saves time/RAM; usually enough to reach the main menu). For a full tree (everything KotOR.js could touch), set `**Module.reoneWebFsNoSkip = true**` before `engine.js`, or set `**Module.reoneWebFsSkipTopDirs = []**` (empty array: no directory skips). `**reoneWebFsSkipTopDirs**` can also list lower-case top-level folder names to skip; omit or leave unset to use the default list above (ignored when `**reoneWebFsNoSkip**` is true).

### Embedded preload (optional; not KotOR.js parity)

KotOR.js does **not** embed the install into the build. ReOne matches that by default (`REONE_WEB_ASSET_PROFILE=none`, `REONE_WEB_GAME_DIR` unset).

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

Custom subsets: same **`REONE_WEB_ALLOW_EMBEDDED_GAME_BUNDLE=ON`** as above, then `-DREONE_WEB_ASSET_PROFILE=custom` plus `-DREONE_WEB_PRELOAD_ITEMS='rel/path/a;rel/path/b'` (CMake list, semicolon-separated).

Default **KotOR.js-aligned** builds embed **no** game files, so `/game` is empty until you pick a folder.

If you use CMake `**full`** / `**custom`** embed, set `**Module.reoneAssumeEmbeddedGamePreload = true`** before `engine.js` so the folder picker is skipped (non-parity). Otherwise `gamefs.js` still tries File System Access and mounting may conflict with existing nodes.

### Experimental: pthreads (not KotOR.js default)

**KotOR.js does not drive the default story around threading.** ReOne keeps `**REONE_WEB_PTHREADS` OFF** by default. Turning it on is **experimental** (different perf/debug surface, COOP/COEP requirements).

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

**Default (FS Access — KotOR.js alignment):**

```bash
python tools/web/serve.py --directory build-web/bin --port 4204
```

Open:

```text
http://localhost:4204/engine.html
```

Use **Choose folder…** and select your KotOR install root (must contain `chitin.key`), e.g. Steam:

```text
...\steamapps\common\swkotor
```

### Optional: HTTP mirror of game files (tooling only)

`serve.py` can also expose a **static mirror** of a game directory (`--game-root`) with `**/game-manifest.json`** and ranged `**/game-files/...`**. That is **not** wired into `gamefs.js` and is **not** KotOR.js parity — useful for tests or external harnesses (see `tools/web/test_serve_smoke.py`).

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

- **FS Access mode**: Confirm you picked the folder that contains `**chitin.key`** (same as KotOR.js). Use a Chromium-class browser.
- **Full/custom embed**: confirm `REONE_WEB_ASSET_PROFILE` and `/game/chitin.key` after preload.

### Blank canvas / shader errors

Web uses the `glsl` folder preloaded at `/glsl` when `shaderpack.erf` is absent. Confirm `glsl` was packaged into the build output as configured in CMake.

## Current limitations

- No full movie (Bink) parity in web builds (`ENABLE_MOVIE` off).
- No MP3 decode in default web flags (`ENABLE_MP3` off).
- File System Access API is not available in Firefox/Safari today; use **CMake embedded preload** for those browsers if you need a non-picker path.
- Performance tuning and asset UX are ongoing.


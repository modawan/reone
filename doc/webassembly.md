# WebAssembly Browser Build

This guide captures the exact workflow used to build and run `reone` in a browser with WebAssembly.

The current browser target is a bring-up build focused on getting `engine` running. Native-only apps/tools are disabled in web mode, and some runtime features are intentionally reduced while parity work continues.

## What Gets Built

- Target: `engine`
- Output directory: `build-web/bin`
- Expected artifacts:
	- `engine.html`
	- `engine.js`
	- `engine.wasm`

## Prerequisites

### Common (all platforms)

- CMake (recommended: recent release)
- Python 3 (for `tools/web/serve.py`)
- Emscripten SDK (`emsdk`)
- Ninja build tool
- Boost headers/libraries available to CMake (for this repo)

### Windows

Install tools:

```powershell
# Ninja (worked in this setup)
winget install -e --id Ninja-build.Ninja --accept-source-agreements --accept-package-agreements

# Emscripten SDK
Set-Location C:\GitHub
git clone https://github.com/emscripten-core/emsdk.git
Set-Location C:\GitHub\emsdk
.\emsdk install latest
.\emsdk activate latest

# Boost via vcpkg (minimum used here)
C:\Users\boden\vcpkg\vcpkg.exe install boost-core --triplet x64-windows
```

If you hit missing Boost headers later, install a broader Boost set:

```powershell
C:\Users\boden\vcpkg\vcpkg.exe install boost --triplet x64-windows
```

### macOS

Install tools:

```bash
brew install cmake ninja python git boost

git clone https://github.com/emscripten-core/emsdk.git
cd emsdk
./emsdk install latest
./emsdk activate latest
source ./emsdk_env.sh
```

### Linux (Debian/Ubuntu example)

Install tools:

```bash
sudo apt update
sudo apt install -y cmake ninja-build python3 git build-essential libboost-all-dev

git clone https://github.com/emscripten-core/emsdk.git
cd emsdk
./emsdk install latest
./emsdk activate latest
source ./emsdk_env.sh
```

For Fedora/Arch, install equivalent packages (`cmake`, `ninja`, `python3`, `boost`, compiler toolchain) and then run the same `emsdk` steps.

## Configure

Run from repository root.

### Preferred cross-platform configure

```bash
emcmake cmake -G Ninja -S . -B build-web -DREONE_PLATFORM_WEB=ON -DCMAKE_BUILD_TYPE=Release
```

### Windows fallback used in this setup

When `emcmake` is not on PATH but emsdk is installed:

```powershell
C:\GitHub\emsdk\upstream\emscripten\emcmake.bat cmake -G Ninja -S . -B build-web -DREONE_PLATFORM_WEB=ON -DCMAKE_BUILD_TYPE=Release -DBoost_INCLUDE_DIR=C:/Users/boden/vcpkg/installed/x64-windows/include
```

### Threaded variant

```bash
emcmake cmake -G Ninja -S . -B build-web-pthreads -DREONE_PLATFORM_WEB=ON -DREONE_WEB_PTHREADS=ON -DCMAKE_BUILD_TYPE=Release
```

Threaded browser builds require COOP/COEP headers at runtime (`Cross-Origin-Opener-Policy: same-origin`, `Cross-Origin-Embedder-Policy: require-corp`).

## Build

```bash
cmake --build build-web --target engine -j 8
```

## Serve

Use the included server (it sets WASM MIME + COOP/COEP/CORP headers):

```bash
python tools/web/serve.py --directory build-web/bin --port 4024
```

Open:

```text
http://localhost:4024/engine.html
```

## Commands Executed During This Implementation

These are the core commands that were actually used and validated in this environment:

```powershell
# Toolchain install / verification
Get-Command emcc -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source
Get-Command emcmake -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source

Set-Location C:\GitHub
git clone https://github.com/emscripten-core/emsdk.git
Set-Location C:\GitHub\emsdk
.\emsdk install latest
.\emsdk activate latest

winget install -e --id Ninja-build.Ninja --accept-source-agreements --accept-package-agreements
Get-Command ninja | Select-Object -ExpandProperty Source

# Boost availability
C:\Users\boden\vcpkg\vcpkg.exe install boost-core --triplet x64-windows

# Configure + build
Set-Location C:\GitHub\reone
C:\GitHub\emsdk\upstream\emscripten\emcmake.bat cmake -G Ninja -S . -B build-web -DREONE_PLATFORM_WEB=ON -DCMAKE_BUILD_TYPE=Release -DBoost_INCLUDE_DIR=C:/Users/boden/vcpkg/installed/x64-windows/include
cmake --build build-web --target engine -j 8

# Run
python tools\web\serve.py --directory C:\GitHub\reone\build-web\bin --port 4024
```

## Troubleshooting

### `emcmake: no compatible cmake generator found`

Install Ninja and re-run configure with `-G Ninja`.

### `Could NOT find Boost` or missing Boost headers

- Install Boost (or at least `boost-core`) through package manager/vcpkg.
- Pass `-DBoost_INCLUDE_DIR=...` on Windows if CMake cannot auto-detect.

### `Could not find SDL3Config.cmake`

This repo is set up to use Emscripten's SDL3 port in web mode, so external SDL3 package discovery is not required for browser builds.

### Browser page loads but runtime fails

- Confirm you are serving through `tools/web/serve.py` (not a basic static server).
- Confirm `engine.html`, `engine.js`, `engine.wasm` exist in `build-web/bin`.

## Current Scope and Limitations

Implemented for web bring-up:

- WebGL2/GLES path and Emscripten main-loop integration
- Browser-safe shader setup for WebGL2 bootstrap
- Localhost serving with required web headers

Still in progress for full parity:

- Full movie playback parity
- Full audio codec parity
- End-to-end asset ingestion/persistence UX in browser
- Browser performance tuning for native-like behavior
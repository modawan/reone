#!/usr/bin/env bash
# Mirror .github/workflows/build-wasm.yml locally (serve smokes + Release wasm link).
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$root"

find_free_port() {
  local p=$1
  while ss -ltn 2>/dev/null | grep -q ":${p} " || lsof -i ":$p" -sTCP:LISTEN -t >/dev/null 2>&1; do
    p=$((p + 1))
  done
  echo "$p"
}

python3 tools/web/test_serve_smoke.py

# Game-mirror integration (no wasm in --directory); matches CI workflow step.
mirror_port=$(find_free_port 28765)
mkdir -p /tmp/web-empty /tmp/gr
printf 'xy' > /tmp/gr/swkotor.exe
python3 tools/web/gen_game_manifest.py /tmp/gr --strict
python3 tools/web/serve.py --directory /tmp/web-empty --game-root /tmp/gr --port "$mirror_port" &
pid=$!
sleep 1
curl -fsS "http://127.0.0.1:${mirror_port}/game-manifest.json" >/dev/null
code=$(curl -s -o /dev/null -w "%{http_code}" -H "Range: bytes=0-0" "http://127.0.0.1:${mirror_port}/game-files/swkotor.exe")
test "$code" = "206"
kill "$pid"

boost_include="${REONE_BOOST_INCLUDE_DIR:-/opt/reone/boost-include}"
if [[ ! -f "$boost_include/boost/version.hpp" ]]; then
  boost_include="/tmp/reone-boost-include"
  mkdir -p "$boost_include"
  cp -a /usr/include/boost "$boost_include/"
fi

if ! command -v emcmake >/dev/null; then
  echo "emcmake not on PATH — source emsdk: source /path/to/emsdk/emsdk_env.sh" >&2
  exit 1
fi

embuilder build sdl3
rm -rf build-web
emcmake cmake -G Ninja -S . -B build-web \
  -DCMAKE_BUILD_TYPE=Release \
  -DBoost_INCLUDE_DIR="$boost_include" \
  -DBoost_NO_BOOST_CMAKE=ON
cmake --build build-web --target engine -j "$(nproc)"
python3 tools/web/verify_wasm_bundle.py build-web/bin

engine_port=$(find_free_port 28766)
python3 tools/web/serve.py --directory build-web/bin --port "$engine_port" &
pid=$!
trap 'kill "$pid" 2>/dev/null || true' EXIT
sleep 1
curl -fsS "http://127.0.0.1:${engine_port}/engine.html" | grep -qi '<html'
echo "ci_build_wasm: ok"

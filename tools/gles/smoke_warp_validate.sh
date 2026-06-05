#!/usr/bin/env bash
# Smoke: headless Xvfb -> engine -> console warp -> module -> screenshot (Mesa GLES IBL check).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
# shellcheck source=tools/gles/headless_x11.sh
source "$ROOT/tools/gles/headless_x11.sh"

if [[ "${REONE_HEADLESS:-0}" != "1" ]]; then
  headless_x11_start
  trap headless_x11_stop EXIT
fi

BUILD_DIR="$ROOT/build-gles"
BINDIR="$BUILD_DIR/bin"
# shellcheck source=tools/gles/assert_engine_bindir.sh
source "$ROOT/tools/gles/assert_engine_bindir.sh"
assert_gles_engine_bindir "$BUILD_DIR" "$BINDIR"
OUTDIR="$ROOT/tools/gles/evidence"
GAME="${GAME:-/run/media/brunner56/MyBook/SteamLibrary/steamapps/common/swkotor}"
MODULE="${MODULE:-danm14aa}"
mkdir -p "$OUTDIR"

if [[ ! -f "$GAME/chitin.key" ]]; then
  echo "KOTOR install not found at $GAME" >&2
  exit 1
fi

echo "Building engine, launcher, shaderpack..."
if [[ "${REONE_SKIP_BUILD:-0}" != "1" ]]; then
  cmake --build "$BUILD_DIR" --target engine launcher shaderpack -j"$(nproc)"
fi

printf 'warp %s\n' "$MODULE" >"$BINDIR/smoke_warp.cmd"

cat >"$BINDIR/reone.cfg" <<EOF
game=$GAME
dev=1
width=1024
height=768
winscale=100
fullscreen=0
vsync=0
grass=0
pbr=1
ssao=0
ssr=0
fxaa=0
sharpen=0
texquality=0
shadowres=1
anisofilter=4
drawdist=64
musicvol=0
voicevol=0
soundvol=0
movievol=0
logsev=1
logch=9
commands-file=smoke_warp.cmd
EOF

cd "$BINDIR"
rm -f engine.log
: >engine.log

if [[ ! -f "$BINDIR/shaderpack.erf" && -f "$ROOT/build/bin/shaderpack.erf" ]]; then
  cp "$ROOT/build/bin/shaderpack.erf" "$BINDIR/shaderpack.erf"
fi
if [[ ! -f "$BINDIR/shaderpack.erf" ]]; then
  echo "Missing shaderpack.erf in $BINDIR" >&2
  exit 1
fi
"$BINDIR/shaderpack" "$ROOT/glsl" "$BINDIR" >/dev/null

stop_bindir_engines() {
  local pid cwd
  for pid in $(pgrep -x engine 2>/dev/null || true); do
    cwd=$(readlink -f "/proc/$pid/cwd" 2>/dev/null || true)
    if [[ "$cwd" == "$(readlink -f "$BINDIR")" ]]; then
      kill "$pid" 2>/dev/null || true
    fi
  done
  pkill -f "$BINDIR/launcher" 2>/dev/null || true
  sleep 1
}
stop_bindir_engines

engine_alive() {
  local pid="$1"
  [[ -n "$pid" ]] && kill -0 "$pid" 2>/dev/null
}

find_engine_window() {
  local pid="$1"
  local wid=""
  wid=$(xdotool search --pid "$pid" 2>/dev/null | head -1 || true)
  if [[ -n "$wid" ]]; then
    echo "$wid"
    return 0
  fi
  wid=$(xdotool search --class engine 2>/dev/null | head -1 || true)
  if [[ -n "$wid" ]]; then
    echo "$wid"
    return 0
  fi
  for wid in $(xdotool search --class SDL_app 2>/dev/null); do
    echo "$wid"
    return 0
  done
  return 1
}

echo "Launching engine (console warp to $MODULE)..."
GDK_BACKEND=x11 SDL_VIDEODRIVER=x11 ./engine >>engine.log 2>&1 &
ENGINE_PID=$!
cleanup() {
  stop_bindir_engines
}
trap cleanup EXIT

for _ in $(seq 1 30); do
  if grep -q "Engine failure:" engine.log 2>/dev/null; then
    echo "Engine failed during startup" >&2
    tail -20 engine.log 2>/dev/null || true
    exit 1
  fi
  if grep -q "reone smoke signal: engine startup" engine.log 2>/dev/null; then
    break
  fi
  if ! engine_alive "$ENGINE_PID"; then
    echo "Engine exited before startup completed" >&2
    tail -20 engine.log 2>/dev/null || true
    exit 1
  fi
  sleep 1
done
if ! grep -q "reone smoke signal: engine startup" engine.log 2>/dev/null; then
  echo "Engine did not log startup" >&2
  tail -20 engine.log 2>/dev/null || true
  exit 1
fi
echo "Engine pid $ENGINE_PID"

ENGINE_WIN=""
for _ in $(seq 1 60); do
  if ! engine_alive "$ENGINE_PID"; then
    echo "Engine exited while searching for window" >&2
    tail -50 engine.log 2>/dev/null || true
    exit 1
  fi
  ENGINE_WIN=$(find_engine_window "$ENGINE_PID" || true)
  [[ -n "$ENGINE_WIN" ]] && break
  sleep 1
done
if [[ -z "$ENGINE_WIN" ]]; then
  echo "Engine window not found" >&2
  exit 1
fi
echo "Engine window $ENGINE_WIN"

# Engine skips frame updates when unfocused; focus the window on Xvfb for rendering.
xdotool windowfocus --sync "$ENGINE_WIN" 2>/dev/null || true
xdotool windowraise "$ENGINE_WIN" 2>/dev/null || true

echo "Waiting for module $MODULE..."
for _ in $(seq 1 120); do
  if ! engine_alive "$ENGINE_PID"; then
    echo "Engine exited during module load" >&2
    tail -50 engine.log 2>/dev/null || true
    exit 1
  fi
  if grep -qi "Module '${MODULE}' loaded successfully" engine.log 2>/dev/null; then
    break
  fi
  sleep 1
done

if ! grep -qi "Loading module.*${MODULE}" engine.log 2>/dev/null; then
  echo "ERROR: module load not detected in engine.log" >&2
  tail -30 engine.log 2>/dev/null || true
  exit 1
fi

sleep 15

if ! engine_alive "$ENGINE_PID"; then
  echo "Engine exited before screenshot" >&2
  tail -50 engine.log 2>/dev/null || true
  exit 1
fi

TAG="fallback"
if grep -q "Cube map array supported: yes" engine.log 2>/dev/null; then
  TAG="oes-fastpath"
fi

SHOT="$OUTDIR/gles-${TAG}-${MODULE}-$(date +%Y%m%d-%H%M%S).png"
import -window "$ENGINE_WIN" "$SHOT"
echo "Screenshot: $SHOT"
grep "Cube map array supported" engine.log || true
grep -i "Loading module" engine.log | tail -3 || true
grep -i "loaded successfully" engine.log | tail -1 || true

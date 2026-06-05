#!/usr/bin/env bash
# Smoke: rebuild -> headless Xvfb -> engine main menu -> screenshot + log checks.
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
PBR="${PBR:-1}"
mkdir -p "$OUTDIR"

if [[ ! -f "$GAME/chitin.key" ]]; then
  echo "KOTOR install not found at $GAME" >&2
  exit 1
fi

echo "Building engine, launcher, shaderpack..."
if [[ "${REONE_SKIP_BUILD:-0}" != "1" ]]; then
  cmake --build "$BUILD_DIR" --target engine launcher shaderpack -j"$(nproc)"
fi

cat >"$BINDIR/reone.cfg" <<EOF
game=$GAME
dev=0
width=1024
height=768
winscale=100
fullscreen=0
vsync=0
grass=0
pbr=$PBR
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
rm -f "$BINDIR/smoke_warp.cmd"

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

echo "Launching engine directly (dev main menu)..."
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

echo "Waiting for main menu to settle..."
for _ in $(seq 1 15); do
  if ! engine_alive "$ENGINE_PID"; then
    echo "Engine exited before main menu was ready" >&2
    tail -50 engine.log 2>/dev/null || true
    exit 1
  fi
  sleep 1
done

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

xdotool windowfocus --sync "$ENGINE_WIN" 2>/dev/null || true
xdotool windowraise "$ENGINE_WIN" 2>/dev/null || true
headless_x11_activate_window "$ENGINE_WIN"
sleep 2

# Keep pointer over the 3D view (away from WARP / menu buttons).
xdotool mousemove --window "$ENGINE_WIN" 420 260
sleep 3

if ! engine_alive "$ENGINE_PID"; then
  echo "Engine exited before screenshot" >&2
  tail -50 engine.log 2>/dev/null || true
  exit 1
fi

if grep -q "Loading module" engine.log 2>/dev/null; then
  echo "ERROR: engine left main menu (warp/module load detected)" >&2
  grep "Loading module" engine.log || true
  exit 1
fi

if grep -q "Failed to load audio clip 'mus_theme" engine.log 2>/dev/null; then
  echo "ERROR: main menu theme audio failed to load" >&2
  grep "Failed to load audio clip 'mus_theme" engine.log || true
  exit 1
fi

TAG="fallback"
if grep -q "Cube map array supported: yes" engine.log 2>/dev/null; then
  TAG="oes-fastpath"
fi

SHOT="$OUTDIR/gles-menu-${TAG}-$(date +%Y%m%d-%H%M%S).png"
import -window "$ENGINE_WIN" "$SHOT"
echo "Screenshot: $SHOT"
grep "Cube map array supported" engine.log || true
echo "Texture not found count: $(grep -c "Texture not found" engine.log || true)"

# Tight crop on Malak torso in the left 3D panel (1024x768 window, 800x600 GUI stretch).
MALAK_CROP="80x220+55+180"
MALAK_MEAN=$(magick "$SHOT" -crop "$MALAK_CROP" +repage -format "%[mean]" info: 2>/dev/null || echo 0)
MALAK_MEAN_INT=${MALAK_MEAN%%.*}
echo "Malak torso ($MALAK_CROP) mean: $MALAK_MEAN"
if [[ "${MALAK_MEAN_INT:-0}" -lt 400 ]]; then
  echo "ERROR: main menu 3D character too dark (mean=$MALAK_MEAN, need >= 400)" >&2
  exit 1
fi

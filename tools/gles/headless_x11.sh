#!/usr/bin/env bash
# Start/stop an isolated Xvfb display for GLES smoke tests (CI-safe, no user session).
#
# Usage (source from other scripts):
#   source "$(dirname "$0")/headless_x11.sh"
#   headless_x11_start
#   trap headless_x11_stop EXIT
#
# Or run a command in a disposable headless session:
#   tools/gles/with_headless_x11.sh tools/gles/smoke_menu_validate.sh
#
# Opt out (use your real DISPLAY — e.g. local debugging on Plasma):
#   REONE_USE_REAL_DISPLAY=1 tools/gles/smoke_menu_validate.sh

headless_x11_require_tools() {
  local missing=()
  if [[ "${REONE_USE_REAL_DISPLAY:-0}" == "1" ]]; then
    return 0
  fi
  command -v Xvfb >/dev/null 2>&1 || missing+=(Xvfb)
  command -v xdpyinfo >/dev/null 2>&1 || missing+=(xdpyinfo)
  if ((${#missing[@]} > 0)); then
    echo "Missing headless tools: ${missing[*]}" >&2
    echo "Fedora: sudo dnf install xorg-x11-server-Xvfb xorg-x11-server-utils" >&2
    echo "Debian/Ubuntu: sudo apt install xvfb x11-utils" >&2
    return 1
  fi
}

headless_x11_find_display() {
  local d
  for d in $(seq 199 250); do
    if [[ ! -S "/tmp/.X11-unix/X${d}" ]]; then
      echo ":${d}"
      return 0
    fi
  done
  echo "No free X display in :199-:250" >&2
  return 1
}

headless_x11_start() {
  if [[ "${REONE_USE_REAL_DISPLAY:-0}" == "1" ]]; then
    echo "Using real DISPLAY=${DISPLAY:-unset} (REONE_USE_REAL_DISPLAY=1)"
    export REONE_HEADLESS=0
    return 0
  fi

  headless_x11_require_tools

  local size="${REONE_HEADLESS_SIZE:-1024x768x24}"
  REONE_XVFB_DISPLAY="${REONE_XVFB_DISPLAY:-$(headless_x11_find_display)}"
  export REONE_XVFB_DISPLAY

  # Software GL via Mesa llvmpipe — works for OpenGL ES 3.0 on Xvfb without a GPU.
  export LIBGL_ALWAYS_SOFTWARE=1
  export MESA_LOADER_DRIVER_OVERRIDE=llvmpipe
  export GALLIUM_DRIVER=llvmpipe
  export GDK_BACKEND=x11
  export SDL_VIDEODRIVER=x11

  Xvfb "$REONE_XVFB_DISPLAY" -screen 0 "$size" -ac +extension GLX +render -noreset >/dev/null 2>&1 &
  REONE_XVFB_PID=$!
  export REONE_XVFB_PID
  export DISPLAY="$REONE_XVFB_DISPLAY"
  export REONE_HEADLESS=1

  local i
  for i in $(seq 1 100); do
    if xdpyinfo -display "$DISPLAY" >/dev/null 2>&1; then
      echo "Headless Xvfb on DISPLAY=$DISPLAY (pid $REONE_XVFB_PID, Mesa llvmpipe)"
      return 0
    fi
    if ! kill -0 "$REONE_XVFB_PID" 2>/dev/null; then
      echo "Xvfb exited before display was ready" >&2
      return 1
    fi
    sleep 0.1
  done

  echo "Timed out waiting for Xvfb on $DISPLAY" >&2
  headless_x11_stop
  return 1
}

headless_x11_stop() {
  if [[ "${REONE_USE_REAL_DISPLAY:-0}" == "1" ]]; then
    return 0
  fi
  if [[ -n "${REONE_XVFB_PID:-}" ]] && kill -0 "$REONE_XVFB_PID" 2>/dev/null; then
    kill "$REONE_XVFB_PID" 2>/dev/null || true
    wait "$REONE_XVFB_PID" 2>/dev/null || true
  fi
  REONE_XVFB_PID=""
}

# Xvfb has no EWMH window manager; activating windows fails and is unnecessary.
headless_x11_activate_window() {
  local win="$1"
  if [[ "${REONE_HEADLESS:-0}" == "1" ]]; then
    return 0
  fi
  xdotool windowactivate --sync "$win"
}

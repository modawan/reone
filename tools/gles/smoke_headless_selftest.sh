#!/usr/bin/env bash
# CI-safe check: isolated Xvfb + Mesa llvmpipe env comes up (no game required).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
# shellcheck source=tools/gles/headless_x11.sh
source "$ROOT/tools/gles/headless_x11.sh"

headless_x11_start
trap headless_x11_stop EXIT

xdpyinfo -display "$DISPLAY" >/dev/null
echo "Headless display OK: DISPLAY=$DISPLAY LIBGL_ALWAYS_SOFTWARE=$LIBGL_ALWAYS_SOFTWARE"

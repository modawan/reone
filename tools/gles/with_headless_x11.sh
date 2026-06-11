#!/usr/bin/env bash
# Run a command inside an isolated Xvfb + Mesa llvmpipe session (never touches :0 / Wayland).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
# shellcheck source=tools/gles/headless_x11.sh
source "$ROOT/tools/gles/headless_x11.sh"

headless_x11_start
trap headless_x11_stop EXIT

if (($# == 0)); then
  echo "Usage: $0 <command> [args...]" >&2
  exit 2
fi

exec "$@"

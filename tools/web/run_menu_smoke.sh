#!/usr/bin/env bash
# Playwright smoke: engine.html -> main menu (needs a real KotOR install).
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$root"
venv="$root/tools/web/.venv"
if [[ ! -x "$venv/bin/python" ]]; then
  python3 -m venv "$venv"
  "$venv/bin/pip" install -q -r "$root/tools/web/requirements-smoke.txt"
  "$venv/bin/python" -m playwright install chromium
fi
game_root="${REONE_WEB_SMOKE_GAME_ROOT:-${REONE_GAME_PATH:-}}"
if [[ -z "$game_root" ]]; then
  game_root="$("$venv/bin/python" "$root/tools/web/discover_game_root.py" 2>/dev/null || true)"
fi
if [[ -z "$game_root" ]]; then
  echo "Set REONE_WEB_SMOKE_GAME_ROOT to your KotOR folder (chitin.key + swkotor.exe)." >&2
  echo "Example: export REONE_WEB_SMOKE_GAME_ROOT=\"/path/to/KotOR\"" >&2
  exit 1
fi
exec "$venv/bin/python" "$root/tools/web/smoke_engine_menu.py" \
  --spawn-serve \
  --game-root "$game_root" \
  --web-bin build-web/bin \
  --timeout "${REONE_MENU_SMOKE_TIMEOUT:-600}" \
  --out "$root/tools/web/_smoke_menu_verify.png" \
  "$@"

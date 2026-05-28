#!/usr/bin/env bash
# Install user systemd unit for the reone WASM Actions runner (see doc/ci-actions-unblock.md).
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
unit_src="$root/tools/web/systemd/reone-wasm-runner.service"
runner_dir=/tmp/reone-actions-runner

if [ ! -x "$runner_dir/run.sh" ]; then
  echo "Runner not configured at $runner_dir — register first (doc/ci-actions-unblock.md)." >&2
  exit 1
fi

mkdir -p "${XDG_CONFIG_HOME:-$HOME/.config}/systemd/user"
cp "$unit_src" "${XDG_CONFIG_HOME:-$HOME/.config}/systemd/user/reone-wasm-runner.service"
systemctl --user daemon-reload
if command -v loginctl >/dev/null; then
  loginctl enable-linger "$(whoami)" 2>/dev/null || true
fi
systemctl --user enable --now reone-wasm-runner.service
systemctl --user status reone-wasm-runner.service --no-pager
echo "Runner service enabled. Check: gh api repos/OpenKotOR/reone/actions/runners"

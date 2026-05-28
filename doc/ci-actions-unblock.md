# Unblocking stuck GitHub Actions (OpenKotOR/reone)

Use this when **Build WASM** (or other workflows) stay `queued` for 15+ minutes with **zero steps** and API `runner_id: 0`.

## Symptoms

- Run status `queued` or `pending`; `updatedAt` stops moving.
- Job labels include `ubuntu-latest` but no runner is assigned.
- Local `./tools/web/ci_build_wasm.sh` is green — not a wasm compile failure.

## Recovery (repo admin)

1. **Force-cancel** the stuck run (UI cancel often fails when no runner was assigned):

   ```bash
   gh api -X POST repos/OpenKotOR/reone/actions/runs/RUN_ID/force-cancel
   ```

2. **Dispatch** a fresh workflow run (no commit required):

   ```bash
   gh workflow run build-wasm.yml --repo OpenKotOR/reone \
     --ref cursor/web-wasm-gles-and-fs-access
   ```

3. **Watch** until `success` or a real failure with logs:

   ```bash
   gh run watch RUN_ID --repo OpenKotOR/reone --exit-status
   ```

4. If still stuck: **Settings → Actions → General** — disable Actions, save, re-enable (clears repo queue state). Check org **billing / spending limits** and concurrent job limits.

## Avoid

- Rapid pushes to the branch (concurrency cancels in-flight runs).
- Docs-only commits that still touch filtered paths if you need a stable run on a fixed SHA.

## Self-hosted runner (current fix)

When GitHub-hosted `ubuntu-latest` never assigns (`runner_id: 0`), register a repo runner and point `wasm-ci` at it:

```bash
# Registration token (admin)
TOKEN=$(gh api --method POST repos/OpenKotOR/reone/actions/runners/registration-token --jq .token)

# Install and configure (Linux x64 example)
mkdir -p /tmp/reone-actions-runner && cd /tmp/reone-actions-runner
curl -fsSL -o runner.tgz https://github.com/actions/runner/releases/download/v2.334.0/actions-runner-linux-x64-2.334.0.tar.gz
tar xzf runner.tgz && rm runner.tgz
./config.sh --url https://github.com/OpenKotOR/reone --token "$TOKEN" \
  --unattended --name "reone-wasm-$(hostname -s)" \
  --labels "self-hosted,linux,x64,wasm-ci" --replace
./run.sh   # or nohup ./run.sh &

# Cancel queued hosted jobs, then dispatch
for id in $(gh run list --repo OpenKotOR/reone --status queued --json databaseId -q '.[].databaseId'); do
  gh api -X POST "repos/OpenKotOR/reone/actions/runs/${id}/force-cancel"
done
gh workflow run build-wasm.yml --repo OpenKotOR/reone --ref cursor/web-wasm-gles-and-fs-access
```

Workflow uses `runs-on: [self-hosted, linux, x64, wasm-ci]` and native Fedora deps (no job container — Podman/docker.sock often blocks containers).

## Parity while blocked

```bash
source /path/to/emsdk/emsdk_env.sh
./tools/web/ci_build_wasm.sh
```

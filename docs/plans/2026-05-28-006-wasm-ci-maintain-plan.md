---
title: "chore: Maintain WASM CI on self-hosted runner"
type: chore
status: completed
date: 2026-05-28
---

# chore: Maintain WASM CI on self-hosted runner

## Summary

Build WASM is green on self-hosted runner (run 26556275588). Harden ops: keep runner alive across reboots, verify HEAD via `workflow_dispatch`, close tracking issue.

## Requirements

- R1. `workflow_dispatch` **success** on HEAD `3302f0eb`.
- R2. Install script + systemd user unit for `reone-wasm` runner under `/tmp/reone-actions-runner`.
- R3. Close [OpenKotOR/reone#3](https://github.com/OpenKotOR/reone/issues/3) with resolution link.

## Out of scope

- Playwright menu smoke (needs retail KotOR install).

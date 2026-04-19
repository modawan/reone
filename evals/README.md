# Smoke Evals

This first migration slice ports only the generic smoke harness. Focused donor evals for interaction, party selection, vitality, combat entry, and K1 interactables are intentionally not included yet.

## Run

Without a legal game install, verify build/executable discovery and record that launch was skipped:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\smoke_test.ps1 -AllowMissingGame
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\eval_smoke.ps1 -SmokeDir .\.agent\logs\smoke_latest -AllowMissingGame
```

With a legal KOTOR 1 install:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\smoke_test.ps1 -GameDir "C:\Path\To\KotOR" -Game k1
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\eval_smoke.ps1 -SmokeDir .\.agent\logs\smoke_latest
```

With a legal KOTOR 2 install:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\smoke_test.ps1 -GameDir "C:\Path\To\KotOR2" -Game k2
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\eval_smoke.ps1 -SmokeDir .\.agent\logs\smoke_latest
```

## Contract

`smoke_test.ps1` writes:

- `stdout.log`
- `stderr.log`
- `engine.log`
- `smoke_manifest.json`

Artifacts are written to `.agent\logs\smoke_<timestamp>` and copied to `.agent\logs\smoke_latest`.

`eval_smoke.ps1` checks:

- Build success or executable discovery.
- Built `engine.exe` discovery.
- Built `shaderpack.erf` discovery.
- Engine process launch when a legal game directory is available.
- The deterministic startup signal `reone smoke signal: engine startup` when launch is attempted.
- Absence of obvious fatal patterns such as `Engine failure`, `FATAL`, `Unhandled exception`, access violations, CMake errors, and failed build markers.

The eval returns a non-zero exit code for deterministic failure.

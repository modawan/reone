# Documentation Log

## 2026-04-19: First Modawan-Baseline Migration Slice

Migration decision:

- `D:\git\reone-modawan-main` is the working repository and the new runtime baseline.
- `D:\reone-master` is a read-only donor/reference repository for selected features only.
- The old branch must not be wholesale merged into modawan.
- Old combat legality, reciprocal hostility, boarding-party hostility persistence, and older combat-runtime compensation patches are intentionally excluded from this first migration slice.
- PR #77 is intentionally excluded from this first migration slice.
- Preserve modawan's stronger combat, items, and runtime behavior unless a later bounded slice proves a specific donor change is still needed.

Donor categories inspected:

- Runtime developer/debug overlay and hotkey tools.
- Eval, smoke, logging, and instrumentation harness.
- Early K1 Endar Spire first-door Trask/Carth behavior.

Bounded first-migration options considered:

1. Tooling-first migration.
   - Leverage: high. Restores repeatable build, run, smoke, log capture, and smoke eval loops before gameplay changes.
   - Risk: low. Touches generic scripts, docs, and a shallow startup log/flush only.
   - Likely files: `.gitignore`, `scripts/build.ps1`, `scripts/run_k1.ps1`, `scripts/run_k2.ps1`, `scripts/smoke_test.ps1`, `scripts/eval_smoke.ps1`, `scripts/capture_logs.ps1`, `evals/README.md`, `src/apps/engine/main.cpp`, `include/reone/system/logger.h`, `src/libs/system/logger.cpp`, `docs/documentation.md`, `docs/plans.md`.
   - Expected K1/K2 impact: positive observability for both games; no gameplay, combat, party, or first-door behavior changes.
2. First-door sequence-first migration.
   - Leverage: medium to high for K1 because it targets the earliest Trask/Carth handoff area directly.
   - Risk: medium. Donor changes are tangled with older script execution instrumentation, trigger debug state, action continuation details, and some combat-facing click logging.
   - Likely files: `src/libs/game/action/docommand.cpp`, `src/libs/game/object/area.cpp`, `src/libs/game/object/trigger.cpp`, `src/libs/game/script/runner.cpp`, `src/libs/script/virtualmachine.cpp`, plus focused docs/eval logs.
   - Expected K1/K2 impact: likely positive for K1 Endar Spire if isolated correctly; intended neutral K2 impact, but script/runtime coupling makes it too broad for the first slice.
3. Combined small-slice migration: tooling + logging + first-door fixes only.
   - Leverage: highest if successful because it would add harness proof and one visible early-game behavior improvement together.
   - Risk: medium to high for a first slice. It mixes infrastructure with gameplay-facing script/trigger behavior, making regressions harder to attribute.
   - Likely files: all tooling-first files plus the first-door files listed above.
   - Expected K1/K2 impact: positive K1 observability and possible Endar Spire improvement; K2 should be neutral, but combined blast radius is larger than needed.

Chosen option:

- Option 1, tooling-first migration.
- Rationale: it is the smallest high-leverage slice and gives modawan its own verification loop before any gameplay donor code is considered.

What was ported:

- Generic Windows PowerShell build/run/smoke/log scripts from the donor harness.
- `evals/README.md` describing the smoke eval contract.
- `.agent/` ignore coverage for generated harness logs.
- A minimal startup signal, `reone smoke signal: engine startup`, emitted immediately after logging initializes.
- Public `Logger::flush()` support so bounded smoke runs persist the startup signal before timeout-based process termination.

What was intentionally not ported:

- Developer overlay rendering and hotkey panels from donor `Game`; modawan already has developer console/tools, and the donor overlay is larger than this slice.
- Early Endar Spire first-door Trask/Carth script, trigger, or action-continuation behavior.
- Old combat legality, reciprocal hostility, boarding-party hostility persistence, combat-entry, and encounter band-aid patches.
- Focused donor eval scripts for interaction, party, vitality, combat, or K1 interactables, because those depend on donor tests and older migration history not present in modawan yet.

Local validation procedure:

- Configure/build from PowerShell:
  - `cd D:\git\reone-modawan-main`
  - `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build.ps1 -Config RelWithDebInfo -Target engine -Configure`
  - To force a specific vcpkg tree, set `$env:VCPKG_ROOT = 'D:\vcpkg'` before running the command, or pass `-CMakeArgs '-DCMAKE_TOOLCHAIN_FILE=D:\vcpkg\scripts\buildsystems\vcpkg.cmake'`.
  - The build script defaults to `D:\vcpkg\scripts\buildsystems\vcpkg.cmake` when `VCPKG_ROOT` is unset and that file exists.
- Smoke test without a game install:
  - `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\smoke_test.ps1 -AllowMissingGame`
  - `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\eval_smoke.ps1 -SmokeDir .\.agent\logs\smoke_latest -AllowMissingGame`
  - This path still builds `engine` and `create_shaderpack`; it only skips launching the engine when no legal KOTOR directory is provided.
- K1 smoke/eval:
  - `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\smoke_test.ps1 -GameDir 'D:\SteamLibrary\steamapps\common\swkotor' -Game k1`
  - `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\eval_smoke.ps1 -SmokeDir .\.agent\logs\smoke_latest`
  - Alternatively, set `$env:KOTOR1_DIR` to a legal KOTOR 1 install directory and omit `-GameDir`.
- K2 smoke/eval:
  - `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\smoke_test.ps1 -GameDir 'D:\SteamLibrary\steamapps\common\Knights of the Old Republic II' -Game k2`
  - `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\eval_smoke.ps1 -SmokeDir .\.agent\logs\smoke_latest`
  - Alternatively, set `$env:KOTOR2_DIR` to a legal KOTOR 2 install directory and omit `-GameDir`.
- Common failure modes:
  - SDL3/vcpkg: this repo uses SDL3 headers and `find_package(SDL3 CONFIG REQUIRED)`, with the vcpkg package name `sdl3` and CMake target `SDL3::SDL3`. A working classic vcpkg install should provide `D:\vcpkg\installed\x64-windows\share\sdl3\SDL3Config.cmake` or equivalent. If CMake reports missing `SDL3Config.cmake` or `sdl3-config.cmake`, install the missing external prerequisite with `& 'D:\vcpkg\vcpkg.exe' install sdl3:x64-windows`, or point `VCPKG_ROOT`/`CMAKE_TOOLCHAIN_FILE` to a vcpkg tree that already has SDL3 for the target triplet.
  - Toolchain detection: if CMake cannot find vcpkg packages that are installed elsewhere, verify `$env:VCPKG_ROOT`, `-DCMAKE_TOOLCHAIN_FILE=...`, and any `-DVCPKG_TARGET_TRIPLET=...` value all refer to the same vcpkg tree/triplet.
  - CMake detection: if `cmake.exe` is not on `PATH`, set `$env:CMAKE_EXE` to the full path, for example `C:\Program Files\CMake\bin\cmake.exe`.
  - Build cache drift: if the `build` directory contains a stale or partial CMake cache, rerun `scripts\build.ps1` with `-Configure`; if that still fails, remove only the local `build` directory and configure again.
  - Game install checks: K1 requires `swkotor.exe` in the supplied directory, and K2 requires `swkotor2.exe`. The smoke harness will skip launch only when `-AllowMissingGame` is used.

Verification results:

- PowerShell syntax parsing passed for `scripts/build.ps1`, `scripts/capture_logs.ps1`, `scripts/eval_smoke.ps1`, `scripts/run_k1.ps1`, `scripts/run_k2.ps1`, and `scripts/smoke_test.ps1`.
- `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build.ps1 -Config RelWithDebInfo -Target engine -Configure`
  - Result: failed during CMake configure because local vcpkg does not provide SDL3 package config files (`SDL3Config.cmake`, `sdl3-config.cmake`, or equivalent).
- `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\smoke_test.ps1 -AllowMissingGame`
  - Result: failed before launch for the same SDL3 configure blocker; smoke manifest was written.
- `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\eval_smoke.ps1 -SmokeDir .\.agent\logs\smoke_latest -AllowMissingGame`
  - Result: failed as expected because the manifest records failed build, missing `engine.exe`, and missing `shaderpack.erf`.
- `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\smoke_test.ps1 -GameDir 'D:\SteamLibrary\steamapps\common\swkotor' -Game k1`
  - Result: failed before launch for the same SDL3 configure blocker; K1 marker path was available.
- `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\eval_smoke.ps1 -SmokeDir .\.agent\logs\smoke_latest`
  - Result after K1 smoke: failed because build did not succeed and launch was not attempted.
- `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\smoke_test.ps1 -GameDir 'D:\SteamLibrary\steamapps\common\Knights of the Old Republic II' -Game k2`
  - Result: failed before launch for the same SDL3 configure blocker; K2 marker path was available.
- `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\eval_smoke.ps1 -SmokeDir .\.agent\logs\smoke_latest`
  - Result after K2 smoke: failed because build did not succeed and launch was not attempted.

Remaining blocker:

- Install or point CMake at SDL3 for the active vcpkg/toolchain environment, then rerun the same build and K1/K2 smoke/eval commands. This slice did not install external dependencies or modify files outside the working repository.

## 2026-04-19: Local Validation Bootstrap Follow-Up

Scope:

- This was a validation/bootstrap step only.
- No donor gameplay behavior, overlay migration, first-door behavior, combat, hostility, boarding-party, or modernization code was ported.
- `scripts\build.ps1` now emits a targeted warning when the active classic vcpkg toolchain is found but `installed\<triplet>\share\sdl3` is missing. CMake still performs the authoritative configure step and writes the configure log.

Latest local environment result:

- Current branch: `port-dev-tools`.
- CMake was found at `C:\Program Files\CMake\bin\cmake.exe`.
- vcpkg toolchain was found at `D:\vcpkg\scripts\buildsystems\vcpkg.cmake`.
- `D:\vcpkg\installed\x64-windows\share\sdl2` exists.
- `D:\vcpkg\installed\x64-windows\share\sdl3` does not exist.
- K1 marker exists at `D:\SteamLibrary\steamapps\common\swkotor\swkotor.exe`.
- K2 marker exists at `D:\SteamLibrary\steamapps\common\Knights of the Old Republic II\swkotor2.exe`.

Latest local validation result:

- PowerShell parser checks passed for all scripts under `scripts`.
- `git diff --check` reported only existing CRLF normalization warnings.
- Configure/build still fails because the active vcpkg tree does not have SDL3 installed for `x64-windows`.
- Generic smoke, K1 smoke, and K2 smoke all stop before launch for the same SDL3 configure blocker.
- Generic, K1, and K2 smoke evals ran against the generated manifests and failed as expected because the build did not succeed, `engine.exe` was not produced, and `shaderpack.erf` was not produced.

## 2026-04-19: Local Validation Closure After SDL3 Install

Scope:

- This was a validation closure step only.
- No donor gameplay behavior, overlay migration, first-door behavior, combat, hostility, boarding-party, or modernization code was ported.
- The previous SDL3 blocker is resolved on this machine because `D:\vcpkg\installed\x64-windows\share\sdl3\SDL3Config.cmake` is now present.

Confirmed local procedure and result:

- Configure/build:
  - `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build.ps1 -Config RelWithDebInfo -Target engine -Configure`
  - Result: passed. CMake configured with `D:\vcpkg\scripts\buildsystems\vcpkg.cmake` and built `D:\Git\reone-modawan-main\build\bin\engine.exe`.
- Smoke test without game install:
  - `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\smoke_test.ps1 -AllowMissingGame`
  - `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\eval_smoke.ps1 -SmokeDir .\.agent\logs\smoke_latest -AllowMissingGame`
  - Result: passed. Launch was skipped by design because no game directory was supplied. `engine.exe` and `shaderpack.erf` were verified in `build\bin`.
- K1 smoke/eval:
  - `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\smoke_test.ps1 -GameDir 'D:\SteamLibrary\steamapps\common\swkotor' -Game k1`
  - `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\eval_smoke.ps1 -SmokeDir .\.agent\logs\smoke_latest`
  - Result: passed. Smoke artifacts were written to `.agent\logs\smoke_20260419_092431`.
- K2 smoke/eval:
  - `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\smoke_test.ps1 -GameDir 'D:\SteamLibrary\steamapps\common\Knights of the Old Republic II' -Game k2`
  - `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\eval_smoke.ps1 -SmokeDir .\.agent\logs\smoke_latest`
  - Result: passed. Smoke artifacts were written to `.agent\logs\smoke_20260419_092459`.

Artifacts confirmed:

- `D:\Git\reone-modawan-main\build\bin\engine.exe`
- `D:\Git\reone-modawan-main\build\bin\shaderpack.erf`

Remaining blocker:

- None for the tooling-first validation slice on this machine.

## 2026-04-19: Runtime Developer Overlay Observability Slice

Scope:

- This is an observability-only runtime overlay slice.
- `D:\reone-master` was used only as a read-only donor/reference for the old developer overlay and hotkey shape.
- No donor gameplay behavior, first-door behavior, combat legality, reciprocal hostility, boarding-party hostility persistence, encounter/combat band-aids, PR #77, Dear ImGui draft work, or modernization code was ported.

Overlay-only options considered, smallest to largest:

1. Status/watch overlay shell.
   - Adds a developer-mode-gated, read-only in-game overlay with a small help banner and compact watch panel.
   - Hotkeys: `Ctrl+Shift+D` toggles the overlay, and `Ctrl+Shift+W` toggles the watch panel.
   - Risk: low. Reads existing runtime state and only mutates overlay visibility flags.
2. Status/watch overlay plus developer event feed.
   - Adds option 1 plus the donor-style feed panel and `addDeveloperEvent` plumbing, initially logging only overlay toggle events.
   - Risk: low to medium. More public game API and event buffer surface.
3. Full donor visual overlay minus gameplay patches.
   - Adds trigger outlines, actor labels, target inspector, event feed, and watched values.
   - Risk: medium. It touches trigger debug state, object inspection, context-action/hostility-adjacent labels, and broader rendering helpers.

Chosen option:

- Option 1, status/watch overlay shell.
- Rationale: it is the smallest high-leverage overlay slice and keeps the implementation read-only, isolated, and easy to disable.

What was ported:

- Developer-mode-gated in-game overlay visibility state in `Game`.
- Donor-style `Ctrl+Shift` hotkey chord handling for overlay tools.
- A small on-screen developer banner showing the overlay hotkeys and existing console/profiler shortcuts.
- A read-only watch panel showing screen, module, area, camera mode, game speed, pause state, relative mouse state, party leader id/tag/HP/position, selected object id/tag/resource/type/HP, and hovered object id/tag/resource/type/HP.

Hotkeys:

- `Ctrl+Shift+D`: toggle the developer observability overlay.
- `Ctrl+Shift+W`: toggle the watch panel. If the overlay is hidden, this also opens it.
- Existing tools remain available: backquote opens the console, `F5` toggles the profiler, `V` toggles in-game camera mode in developer mode, and `+`/`-` adjust developer game speed.

How to use:

- Launch with developer mode enabled, for example `scripts\run_k1.ps1 -Dev` or `scripts\run_k2.ps1 -Dev`, or enable Developer Mode in the launcher.
- Enter an in-game module.
- Press `Ctrl+Shift+D` to show or hide the overlay.
- Press `Ctrl+Shift+W` to show or hide the watch panel.

How to disable:

- Launch with developer mode disabled, for example omit `-Dev` from the run scripts or pass `--dev false` to `engine.exe`.
- While in game, press `Ctrl+Shift+D` to hide the overlay.
- Press `Ctrl+Shift+W` to hide only the watch panel while leaving the banner visible.

What was intentionally not ported:

- Donor trigger outlines and trigger debug state.
- Donor actor labels.
- Donor target inspector.
- Donor event feed and `addDeveloperEvent` instrumentation.
- Donor script/action/combat/area/creature instrumentation.
- Any donor gameplay behavior, hostility, party, item, cutscene, encounter, or first-door logic.

Validation results:

- `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build.ps1 -Config RelWithDebInfo -Target engine -Configure`
  - Result: passed. `D:\Git\reone-modawan-main\build\bin\engine.exe` was produced.
- `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\smoke_test.ps1 -AllowMissingGame`
  - Result: passed. Launch was skipped by design; smoke artifacts were written to `.agent\logs\smoke_20260419_093609`.
- `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\eval_smoke.ps1 -SmokeDir .\.agent\logs\smoke_latest -AllowMissingGame`
  - Result: passed.
- `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\smoke_test.ps1 -GameDir 'D:\SteamLibrary\steamapps\common\swkotor' -Game k1`
  - Result: passed. Smoke artifacts were written to `.agent\logs\smoke_20260419_093617`.
- `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\eval_smoke.ps1 -SmokeDir .\.agent\logs\smoke_latest`
  - Result after K1 smoke: passed.
- `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\smoke_test.ps1 -GameDir 'D:\SteamLibrary\steamapps\common\Knights of the Old Republic II' -Game k2`
  - Result: passed. Smoke artifacts were written to `.agent\logs\smoke_20260419_093630`.
- `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\eval_smoke.ps1 -SmokeDir .\.agent\logs\smoke_latest`
  - Result after K2 smoke: passed.
- `git diff --check`
  - Result: no whitespace errors; only CRLF normalization warnings.

Human verification needed:

- Automated smoke validates startup and the smoke signal, but it does not press overlay hotkeys.
- Human in-game verification should launch with developer mode enabled and confirm `Ctrl+Shift+D` shows/hides the overlay and `Ctrl+Shift+W` shows/hides the watch panel.

## 2026-04-19: Trigger Zone Developer Overlay Slice

Scope:

- This is an observability-only extension to the developer overlay.
- `D:\reone-master` was used only as a read-only donor/reference for the old trigger visualization, entity label, and launcher developer-option wiring.
- No donor gameplay behavior, first-door behavior, combat legality, reciprocal hostility, boarding-party hostility persistence, encounter/combat band-aids, PR #77, Dear ImGui draft work, or modernization code was ported.

Options considered:

1. A: trigger zone visualization only.
   - Leverage: high. Restores the most visible missing spatial observability from the old branch.
   - Risk: low. Reads existing trigger geometry and draws screen-space outlines/labels only.
   - Likely files: `include/reone/game/object/trigger.h`, `include/reone/game/game.h`, `src/libs/game/game.cpp`, docs.
   - Runtime blast radius: developer-mode render path only.
   - Disable: launch without developer mode, press `Ctrl+Shift+D` to hide the overlay, or press `Ctrl+Shift+T` to hide triggers.
2. B: entity/world labels only.
   - Leverage: medium to high. Useful for object inspection, faction-style debugging, and hover/selection review.
   - Risk: medium. Rich labels tend to pull in creature, faction, hostility, and context-action interpretation.
   - Likely files: `include/reone/game/game.h`, `src/libs/game/game.cpp`, docs.
   - Runtime blast radius: developer-mode render path over area objects.
   - Disable: launch without developer mode, hide the overlay, or add a labels toggle.
3. C: minimal launcher-level developer options wiring.
   - Leverage: medium for workflow, low for immediate in-game observability because Developer Mode already exists in the launcher.
   - Risk: low. Launcher/config surface only.
   - Likely files: `src/apps/launcher/frame.h`, `src/apps/launcher/frame.cpp`, docs.
   - Runtime blast radius: launcher/config only.
   - Disable: uncheck launcher options.

Chosen option:

- Option A, trigger zone visualization only.
- Rationale: it closes the largest manually observed overlay gap with the smallest runtime surface and avoids the faction/hostility-adjacent data needed for useful entity labels.

What was added:

- A read-only `Trigger::geometry()` accessor so the overlay can inspect existing trigger polygon points without changing trigger behavior.
- `Ctrl+Shift+T` developer hotkey for trigger zone visualization.
- Screen-space trigger polygon outlines in the developer overlay.
- Trigger labels at the polygon centroid showing object id, tag, and `OnEnter` script when available.

How to enable:

- Launch with developer mode enabled, for example `scripts\run_k1.ps1 -Dev`, `scripts\run_k2.ps1 -Dev`, or the launcher Developer Mode checkbox.
- Enter an in-game module.
- Press `Ctrl+Shift+D` to show the developer overlay. Trigger visualization is enabled by default when the overlay is shown.
- Press `Ctrl+Shift+T` to show or hide trigger zones. If the overlay is hidden, this hotkey also opens it.

Hotkeys:

- `Ctrl+Shift+D`: toggle the developer overlay.
- `Ctrl+Shift+T`: toggle trigger zone visualization.
- `Ctrl+Shift+W`: toggle the watch panel.

How to disable:

- Launch without developer mode.
- Press `Ctrl+Shift+D` to hide the whole overlay.
- Press `Ctrl+Shift+T` to hide trigger zones while keeping the rest of the overlay visible.

What was intentionally not ported:

- Donor trigger debug state, colors, occupancy/test/enter timers, or Area/Trigger instrumentation.
- Donor entity/world actor labels.
- Donor target inspector.
- Donor event feed and `addDeveloperEvent` instrumentation.
- Any launcher UI redesign or new launcher controls.
- Any gameplay, combat, hostility, boarding-party, encounter, item, cutscene, party, or first-door behavior.

Validation results:

- Default build output `D:\Git\reone-modawan-main\build\bin\engine.exe` was locked by a stale `engine` process from manual verification. PowerShell could see process id `112868` but could not stop it due access permissions.
- Validation was completed with a clean ignored build directory: `.\build\overlay_validation`.
- `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build.ps1 -BuildDir .\build\overlay_validation -Config RelWithDebInfo -Target engine -Configure`
  - Result: passed. `D:\Git\reone-modawan-main\build\overlay_validation\bin\engine.exe` was produced.
- `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\smoke_test.ps1 -BuildDir .\build\overlay_validation -AllowMissingGame`
  - Result: passed. Launch was skipped by design.
- `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\eval_smoke.ps1 -SmokeDir .\.agent\logs\smoke_latest -AllowMissingGame`
  - Result: passed.
- `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\smoke_test.ps1 -BuildDir .\build\overlay_validation -GameDir 'D:\SteamLibrary\steamapps\common\swkotor' -Game k1`
  - Result: passed. Smoke artifacts were written to `.agent\logs\smoke_20260419_122754`.
- `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\eval_smoke.ps1 -SmokeDir .\.agent\logs\smoke_latest`
  - Result after K1 smoke: passed.
- `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\smoke_test.ps1 -BuildDir .\build\overlay_validation -GameDir 'D:\SteamLibrary\steamapps\common\Knights of the Old Republic II' -Game k2`
  - Result: passed. Smoke artifacts were written to `.agent\logs\smoke_20260419_122825`.
- `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\eval_smoke.ps1 -SmokeDir .\.agent\logs\smoke_latest`
  - Result after K2 smoke: passed.

Human verification needed:

- Automated smoke validates startup, but it does not press overlay hotkeys.
- Human in-game verification should launch with developer mode enabled and confirm `Ctrl+Shift+D` shows/hides the overlay and `Ctrl+Shift+T` shows/hides trigger outlines and labels.

Default build retry after closing game:

- The previous default build lock was caused by a running `engine.exe` from manual verification.
- After that process was closed, the normal default build directory validated successfully.
- `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build.ps1 -Config RelWithDebInfo -Target engine -Configure`
  - Result: passed. `D:\Git\reone-modawan-main\build\bin\engine.exe` was produced.
- `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\smoke_test.ps1 -AllowMissingGame`
  - Result: passed. Launch was skipped by design.
- `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\eval_smoke.ps1 -SmokeDir .\.agent\logs\smoke_latest -AllowMissingGame`
  - Result: passed.
- `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\smoke_test.ps1 -GameDir 'D:\SteamLibrary\steamapps\common\swkotor' -Game k1`
  - Result: passed. Smoke artifacts were written to `.agent\logs\smoke_20260419_123202`.
- `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\eval_smoke.ps1 -SmokeDir .\.agent\logs\smoke_latest`
  - Result after K1 smoke: passed.
- `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\smoke_test.ps1 -GameDir 'D:\SteamLibrary\steamapps\common\Knights of the Old Republic II' -Game k2`
  - Result: passed. Smoke artifacts were written to `.agent\logs\smoke_20260419_123230`.
- `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\eval_smoke.ps1 -SmokeDir .\.agent\logs\smoke_latest`
  - Result after K2 smoke: passed.

## 2026-04-19: Developer Overlay Gap-Fix Slice

Scope:

- This is an observability-only follow-up to the trigger zone overlay slice.
- `D:\reone-master` was used only as a read-only donor/reference for old trigger state coloring and lightweight entity labels.
- No donor gameplay behavior, first-door behavior, combat legality, reciprocal hostility, boarding-party hostility persistence, encounter/combat band-aids, PR #77, Dear ImGui draft work, launcher redesign, or modernization code was ported.

What was added:

- Trigger zones now expose read-only debug state in the overlay:
  - `default`: normal blue outline.
  - `tested`: yellow outline after a creature movement tested the trigger.
  - `inside`: green outline while an object is inside or was just detected inside.
  - `enter`: orange outline briefly after a trigger enters/fires.
- Trigger labels now include the debug state, for example `#230 end_trig02 k_pend_trig02 [inside]`.
- Lightweight entity/world labels can be toggled with `Ctrl+Shift+A`.
- Entity labels show read-only id, tag, template, faction, hostile-to-player flag for creatures, selectable flag, commandable flag, visible flag, and plot flag.
- Door and placeable faction accessors were added only so the developer label can display existing loaded faction data.

Hotkeys:

- `Ctrl+Shift+D`: toggle the developer overlay.
- `Ctrl+Shift+T`: toggle trigger zones and trigger state labels.
- `Ctrl+Shift+A`: toggle entity/world labels.
- `Ctrl+Shift+W`: toggle the watch panel.

How to enable:

- Launch with developer mode enabled, for example `scripts\run_k1.ps1 -Dev`, `scripts\run_k2.ps1 -Dev`, or the launcher Developer Mode checkbox.
- Enter an in-game module.
- Press `Ctrl+Shift+D` to show the developer overlay.
- Press `Ctrl+Shift+T` for trigger zones and `Ctrl+Shift+A` for entity labels. If the overlay is hidden, those feature hotkeys open the overlay with the requested feature enabled.

How to disable:

- Launch without developer mode.
- Press `Ctrl+Shift+D` to hide the whole overlay.
- Press `Ctrl+Shift+T` to hide trigger zones.
- Press `Ctrl+Shift+A` to hide entity labels.

What was intentionally not ported:

- Donor target inspector.
- Donor event feed and broad `addDeveloperEvent` instrumentation.
- Donor trigger/area encounter logging and first-door-specific trigger diagnostics.
- Donor launcher developer-option controls.
- Any gameplay, combat, hostility, boarding-party, encounter, item, cutscene, party, first-door, or modernization behavior.

Validation:

- Default build note:
  - `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build.ps1 -Config RelWithDebInfo -Target engine -Configure`
  - Result: compile passed, but the final link failed with `LNK1168: cannot open D:\Git\reone-modawan-main\build\bin\engine.exe for writing`, indicating the default output executable was locked locally.
- Default build retry:
  - `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build.ps1 -Config RelWithDebInfo -Target engine`
  - Result: passed after the local executable lock was gone. `D:\Git\reone-modawan-main\build\bin\engine.exe` now contains the `Ctrl+Shift+A` label hotkey.
- Hotkey follow-up:
  - Feature hotkeys now open the overlay with that feature enabled when the overlay is hidden, instead of toggling a default-on feature off.
  - `Ctrl+Shift+T`, `Ctrl+Shift+A`, and `Ctrl+Shift+W` keep their normal toggle behavior while the overlay is already visible.
- Validation build:
  - `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build.ps1 -BuildDir .\build\overlay_labels_validation -Config RelWithDebInfo -Target engine -Configure`
  - Result: passed. `D:\Git\reone-modawan-main\build\overlay_labels_validation\bin\engine.exe` was produced.
- Smoke test without game install:
  - `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\smoke_test.ps1 -BuildDir .\build\overlay_labels_validation -AllowMissingGame`
  - `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\eval_smoke.ps1 -SmokeDir .\.agent\logs\smoke_latest -AllowMissingGame`
  - Result: passed. Launch was skipped by design.
- K1 smoke/eval:
  - `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\smoke_test.ps1 -BuildDir .\build\overlay_labels_validation -GameDir 'D:\SteamLibrary\steamapps\common\swkotor' -Game k1`
  - `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\eval_smoke.ps1 -SmokeDir .\.agent\logs\smoke_latest`
  - Result: passed. Smoke artifacts were written to `.agent\logs\smoke_20260419_130346`.
- K2 smoke/eval:
  - `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\smoke_test.ps1 -BuildDir .\build\overlay_labels_validation -GameDir 'D:\SteamLibrary\steamapps\common\Knights of the Old Republic II' -Game k2`
  - `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\eval_smoke.ps1 -SmokeDir .\.agent\logs\smoke_latest`
  - Result: passed. Smoke artifacts were written to `.agent\logs\smoke_20260419_130425`.
- Artifact check:
  - `D:\Git\reone-modawan-main\build\overlay_labels_validation\bin\engine.exe` exists.
  - `D:\Git\reone-modawan-main\build\overlay_labels_validation\bin\shaderpack.erf` exists.
- `git diff --check`
  - Result: no whitespace errors; only CRLF normalization warnings.
- Default no-game smoke follow-up:
  - `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\smoke_test.ps1 -AllowMissingGame`
  - `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\eval_smoke.ps1 -SmokeDir .\.agent\logs\smoke_latest -AllowMissingGame`
  - Result: passed. Launch was skipped by design.

Human verification needed:

- Automated smoke validates startup, but it does not press overlay hotkeys or move through trigger volumes.
- Human in-game verification should launch with developer mode enabled and confirm `Ctrl+Shift+A` shows/hides entity labels, and trigger zones change to `tested`, `inside`, and `enter` states when crossed.

## 2026-04-19: K1 Early Carth Cutscene / Trigger Delayed-Action Parity Slice

Bug description:

- In K1's early Endar Spire handoff path, the Carth-related message/cutscene progression after the first-room/Trask-door band can fail to appear.
- The old branch identified this as a shared engine delayed-action issue rather than a Carth content hack: `k_pend_trig02` runs with the trigger as caller and schedules a `DelayCommand`; that delayed action is owned by the trigger object.
- Before the old fix, trigger objects ran tenant maintenance but skipped base `Object::update(dt)`, so trigger-owned delayed actions did not mature or execute.

Donor fix source:

- `D:\reone-master\docs\documentation.md`, lines 2440-2506: old diagnosis and implementation summary.
- `D:\reone-master\docs\plans.md`, lines 1290-1332: tiny milestone describing the selected fix.
- `D:\reone-master\src\libs\game\object\trigger.cpp`, lines 163-165: `Trigger::update` calls `Object::update(dt)` before trigger tenant maintenance.

Minimal port:

- Ported only the causal runtime behavior: `Trigger::update(float dt)` now calls `Object::update(dt)` before developer debug timers and tenant exit maintenance.
- This lets trigger-owned action queues, delayed actions, and effects process through the existing modawan base object update path.

What was intentionally not ported:

- Donor scoped `DelayCommand` scheduling logs in script routines.
- Donor `DoCommandAction` execution/result logs.
- Donor live-log eval gates and old K1 combat-entry legal-data tests.
- Donor first-Sith combat, hostility, reciprocal-hostility, boarding-party persistence, encounter band-aids, target inspector/event feed, launcher changes, or content hacks.

Why those were excluded:

- The minimal causal fix is the base `Object::update(dt)` call on trigger objects.
- The donor logging/eval changes were useful during old investigation but are not required to restore the delayed action behavior in this bounded parity slice.
- The excluded combat/hostility and encounter changes belong to older compensating runtime work and are outside this single-bug branch.

Validation:

- Build:
  - `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build.ps1 -Config RelWithDebInfo -Target engine -Configure`
  - Result: passed. `D:\git\reone-modawan-main\build\bin\engine.exe` was produced.
- Smoke test without game install:
  - `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\smoke_test.ps1 -AllowMissingGame`
  - `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\eval_smoke.ps1 -SmokeDir .\.agent\logs\smoke_latest -AllowMissingGame`
  - Result: passed. Launch was skipped by design.
- K1 smoke/eval:
  - `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\smoke_test.ps1 -GameDir 'D:\SteamLibrary\steamapps\common\swkotor' -Game k1`
  - `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\eval_smoke.ps1 -SmokeDir .\.agent\logs\smoke_latest`
  - Result: passed. Smoke artifacts were written to `.agent\logs\smoke_20260419_143200`.
- K2 smoke/eval:
  - `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\smoke_test.ps1 -GameDir 'D:\SteamLibrary\steamapps\common\Knights of the Old Republic II' -Game k2`
  - `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\eval_smoke.ps1 -SmokeDir .\.agent\logs\smoke_latest`
  - Result: passed. Smoke artifacts were written to `.agent\logs\smoke_20260419_143241`.
- Artifact check:
  - `D:\git\reone-modawan-main\build\bin\engine.exe` exists.
  - `D:\git\reone-modawan-main\build\bin\shaderpack.erf` exists.
- `git diff --check`
  - Result: no whitespace errors; only CRLF normalization warnings.

Human verification needed:

- Automated smoke verifies startup only; it does not drive the Endar Spire sequence.
- Launch K1 with the rebuilt engine, play through the early Endar Spire first-room/Trask-door/Carth handoff band without console or forced combat, and confirm the previously missing Carth-related message/cutscene progression occurs.

## 2026-04-19: K1 Early Trask Auto-Dialogue Dispatch Slice

Bug description:

- In K1's early Endar Spire Trask sequence, the scripted follow-up conversations can fail to auto-play even though manual conversation with Trask reaches the expected dialogue state.
- The visible symptoms are the missing unlock-door guidance after the party-management screen closes and the missing "you better take the lead" follow-up after Trask opens the door.
- This points at scripted conversation dispatch, not dialogue-state progression, combat, hostility, boarding-party, or encounter behavior.

Donor source checked:

- `D:\reone-master\docs\documentation.md`, lines 2413-2506: old `END_TRASK_DLG` consumer diagnosis and the trigger-owned delayed-action fix.
- `D:\reone-master\docs\plans.md`, lines 1290-1360: the tiny K1 handoff parity milestone for trigger-owned delayed actions.
- `D:\reone-master\src\libs\game\object\trigger.cpp`, lines 163-165: donor `Trigger::update(float dt)` calls `Object::update(dt)`.
- `D:\reone-master\src\libs\game\action\startconversation.cpp`, lines 30-43: donor scripted conversation action dispatches through `Area::startDialog`.
- `D:\reone-master\src\libs\game\object\area.cpp`, lines 960-968: donor still has the same empty-resref fallback guard defect, so there was no additional donor code patch to copy for this auto-dialogue-specific failure.

Minimal fix:

- Kept the already-integrated trigger delayed-action fix as-is.
- Fixed the shared conversation dispatcher in `src\libs\game\object\area.cpp` so `Area::startDialog` validates the resolved dialogue resref after falling back to `object->conversation()`.
- This allows scripted `ActionStartConversation` calls with an empty dialogue resref to use the target object's default conversation, matching the manual-talk path.
- After live verification showed the first Trask callback still did not auto-play after party selection, fixed `src\libs\game\gui\partyselect.cpp` so the party-selection exit script runs as the current party leader/player instead of running with no caller.
- This gives K1's `k_pend_reset` a valid `OBJECT_SELF` for its caller-sensitive `ClearAllActions()` and follow-up conversation assignment without changing sequence content.

What was intentionally not ported:

- Donor party-selection forced-companion integrity work.
- Donor party roster/HUD logging and party leader observability.
- Donor delayed-command logging/eval instrumentation beyond what is already in this branch.
- Donor combat legality, reciprocal hostility, boarding-party persistence, encounter band-aids, first-Sith hostility work, Carth content changes, launcher changes, or modernization.

Why those were excluded:

- The missing auto-dialogue shape is explained by two shared dispatch issues: the conversation fallback returned before `Game::startDialog` could run, and the party-selection exit script callback ran without a caller.
- The trigger delayed-action donor fix was already integrated and validated in the previous slice.
- The old party-selection donor work addresses roster corruption around the Trask join flow, not this empty-dialog-resref dispatch failure.
- No K1 content hack or gameplay/runtime combat change is needed for this slice.

Validation:

- Build:
  - `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build.ps1 -Config RelWithDebInfo -Target engine -Configure`
  - Result: passed. `D:\git\reone-modawan-main\build\bin\engine.exe` was produced.
- Smoke test without game install:
  - `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\smoke_test.ps1 -AllowMissingGame`
  - `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\eval_smoke.ps1 -SmokeDir .\.agent\logs\smoke_latest -AllowMissingGame`
  - Result: passed. Launch was skipped by design.
- K1 smoke/eval:
  - `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\smoke_test.ps1 -GameDir 'D:\SteamLibrary\steamapps\common\swkotor' -Game k1`
  - `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\eval_smoke.ps1 -SmokeDir .\.agent\logs\smoke_latest`
  - Result: passed after the party-selection caller follow-up. Smoke artifacts were written to `.agent\logs\smoke_20260419_154359`.
- K2 smoke/eval:
  - `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\smoke_test.ps1 -GameDir 'D:\SteamLibrary\steamapps\common\Knights of the Old Republic II' -Game k2`
  - `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\eval_smoke.ps1 -SmokeDir .\.agent\logs\smoke_latest`
  - Result: passed after the party-selection caller follow-up. Smoke artifacts were written to `.agent\logs\smoke_20260419_154438`.
- Artifact check:
  - `D:\git\reone-modawan-main\build\bin\engine.exe` exists.
  - `D:\git\reone-modawan-main\build\bin\shaderpack.erf` exists.
- `git diff --check`
  - Result: no whitespace errors; only CRLF normalization warnings.

Human verification needed:

- Automated smoke verifies startup only; it does not drive the Endar Spire sequence.
- Launch K1 with the rebuilt engine, reach the Trask join point, close party management, and confirm the unlock-door guidance auto-plays without manually talking to Trask.
- Let Trask open the first door without manually forcing the dialogue first, then confirm the "you better take the lead" follow-up auto-plays.
- Do not use console commands, forced combat, faction edits, or unusual routing for the parity check.

Live verification follow-up:

- Human testing confirmed the Carth-side trigger delayed-action fix works, but the first Trask auto-dialogue still did not play immediately after Trask joins the party.
- The remaining first-half cause is a separate shared callback issue: the party-selection GUI ran its configured exit script with no caller, while K1's `k_pend_reset` expects a valid `OBJECT_SELF` before it can pass `ClearAllActions()` and assign Trask's follow-up conversation.
- The minimal follow-up fix keeps the sequence content untouched and runs the party-selection exit script as the current party leader/player after the party screen closes.
- This avoids running the callback as the pre-party Trask object, which could cancel the delayed cleanup queued by the join script.
- No donor combat, hostility, boarding-party, encounter, first-door content, party roster, or modernization code was ported.

## 2026-04-19: Temporary K1 Trask Auto-Dialogue Trace

Purpose:

- This is a temporary instrumentation slice only.
- It is meant to identify the exact live failure point after Trask joins the party and the party-selection screen closes.
- It does not intentionally change gameplay, combat, hostility, encounter, party roster, item, cutscene, or dialogue content behavior.

Temporary trace prefix:

- All added trace lines use `reone trask autodialog trace:` so they can be filtered from `build\bin\engine.log`.

Temporary trace points:

- `src\libs\game\script\routine\impl\main.cpp`
  - `ShowPartySelectionGUI`: logs only when the exit script is `k_pend_reset`; records forced NPC args, allow-cancel arg, caller object, and parent script args.
  - `AssignCommand`: logs when the command subject is a Trask-like object; records the subject and saved action context.
- `src\libs\game\gui\partyselect.cpp`
  - `PartySelection::prepare`: logs only for `k_pend_reset`; records forced NPCs and current party state.
  - `BTN_DONE` close callback: logs that Done closed party selection before `changeParty()`.
  - `BTN_BACK` close callback: logs that Back closed party selection without `changeParty()`.
  - `PartySelection::runExitScript`: logs the exact exit script, selected caller, and party state before dispatch.
- `src\libs\game\script\runner.cpp`
  - `ScriptRunner::run`: logs begin/missing/end only for `k_pend_reset`, including script args and VM result.
- `src\libs\script\virtualmachine.cpp`
  - `VirtualMachine::executeACTION`: logs begin/end only inside script `k_pend_reset` and only for `ClearAllActions`, `GetPartyMemberByIndex`, `AssignCommand`, `ActionStartConversation`, and `SetGlobalNumber`.
- `src\libs\game\action\startconversation.cpp`
  - `StartConversationAction::execute`: logs only when the actor or target is Trask-like; records reached/not-reached, actor, target, dialog resref, and range-ignore flag.
- `src\libs\game\object\area.cpp`
  - `Area::startDialog`: logs only when the dialogue owner is Trask-like; records input resref and resolved final resref.

Manual evidence capture:

- Rebuild the engine.
- Launch `D:\git\reone-modawan-main\build\bin\launcher.exe`.
- Start K1, play normally until Trask joins and party management closes.
- Close the game after confirming whether the unlock-door dialogue auto-plays.
- Inspect `D:\git\reone-modawan-main\build\bin\engine.log` and filter for `reone trask autodialog trace:`.

Hypotheses tested:

- No `ShowPartySelectionGUI` trace means the live path is not using the expected K1 party-selection script call.
- `ShowPartySelectionGUI` without `PartySelection::prepare` means the GUI context is not reaching the party-selection screen.
- `PartySelection::prepare` without a close-path trace means the screen is closing through an uninstrumented path.
- Close-path trace without `runExitScript` means the exit-script dispatch helper is not reached.
- `runExitScript` without `ScriptRunner::run begin` means dispatch does not enter the script runner.
- Script runner begin without VM actions means `k_pend_reset` is missing, empty, or halting before the traced calls.
- VM `AssignCommand` without later `ActionStartConversation` or `StartConversationAction` means the assigned action is not executing.
- `StartConversationAction` without `Area::startDialog` means the action is blocked before dialogue dispatch, likely by range/navigation.
- `Area::startDialog` with an empty final resref means dialogue owner/default conversation resolution is still wrong.

## 2026-04-19: K1 Trask Auto-Dialogue Action Continuation Fix

Traced failure point:

- Live trace proved `ShowPartySelectionGUI("k_pend_reset", 0, -1)` runs and the party-selection screen closes through `BTN_DONE`.
- `k_pend_reset` runs, resolves the new party Trask through `GetPartyMemberByIndex(1)`, and queues two `AssignCommand` actions on Trask.
- The first assigned action starts on Trask and runs `ClearAllActions()`.
- After that clear, the follow-up assigned action that should reach `ActionStartConversation()` does not execute.

Actual engine cause:

- `Object::clearAllActions()` cleared the full action queue even when called from an action currently executing on that same object.
- For this sequence, the first queued `DoCommandAction` resumed at `ClearAllActions()`. The queue still contained that active action and the next queued `DoCommandAction` carrying the follow-up conversation.
- Clearing the whole queue erased the current action frame and the queued continuation behind it, so the action chain died before the saved `ActionStartConversation()` state could run.

Minimal fix:

- `Object::executeActions()` now records the currently executing action while it calls `Action::execute()`.
- `Object::clearAllActions(false)` now preserves the currently executing action and the continuation actions behind it when the clear is invoked from that active action frame.
- Forced clears still clear the queue, and clears outside active action execution keep the existing queue-clearing behavior.
- This is a shared action semantics fix; it has no Trask-specific, Endar-Spire-specific, dialogue-content-specific, combat, hostility, encounter, boarding-party, item, or modernization branch.

Temporary instrumentation status:

- The temporary `reone trask autodialog trace:` logs remain in this validation build so the next live run can prove whether the preserved continuation reaches `ActionStartConversation()` and `Area::startDialog()`.
- Once live verification confirms the exact path, remove or reduce the temporary trace lines.

What was intentionally not changed:

- No K1 content scripts, dialogue files, module data, Trask-specific special cases, combat legality, reciprocal hostility, boarding-party hostility persistence, encounter sequencing, item behavior, PR #77 work, Dear ImGui work, or modernization were ported or added.
- The earlier party-selection caller patch is not treated as the causal live fix; the latest trace shows the real blocker is the action queue clear inside the assigned command chain. It can be reconsidered after live validation of the engine fix.

Validation:

- Build:
  - `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build.ps1 -Config RelWithDebInfo -Target engine -Configure`
  - Result: passed. `D:\git\reone-modawan-main\build\bin\engine.exe` was rebuilt.
- Smoke test without game install:
  - `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\smoke_test.ps1 -AllowMissingGame`
  - `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\eval_smoke.ps1 -SmokeDir .\.agent\logs\smoke_latest -AllowMissingGame`
  - Result: passed. Launch was skipped by design.
- K1 smoke/eval:
  - `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\smoke_test.ps1 -GameDir 'D:\SteamLibrary\steamapps\common\swkotor' -Game k1`
  - `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\eval_smoke.ps1 -SmokeDir .\.agent\logs\smoke_latest`
  - Result: passed. Smoke artifacts were written to `.agent\logs\smoke_20260419_161924`.
- K2 smoke/eval:
  - `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\smoke_test.ps1 -GameDir 'D:\SteamLibrary\steamapps\common\Knights of the Old Republic II' -Game k2`
  - `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\eval_smoke.ps1 -SmokeDir .\.agent\logs\smoke_latest`
  - Result: passed. Smoke artifacts were written to `.agent\logs\smoke_20260419_162011`.
- `git diff --check`
  - Result: no whitespace errors; only CRLF normalization warnings.

Human verification needed:

- Automated smoke verifies startup only; it does not drive the Trask sequence.
- Launch K1 with the rebuilt engine, close party selection after Trask joins, and confirm whether the unlock-door dialogue auto-plays.
- Filter `D:\git\reone-modawan-main\build\bin\engine.log` for `reone trask autodialog trace:` and confirm the preserved continuation reaches `ActionStartConversation` and `Area::startDialog`.

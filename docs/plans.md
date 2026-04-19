# Plans

## Tiny Migration Milestone: Tooling-First Harness

Goal: give the modawan baseline its own bounded build, launch, smoke, log-capture, and smoke-eval loop before any gameplay donor code is considered.

Acceptance criteria:

- `scripts/build.ps1`, `scripts/run_k1.ps1`, `scripts/run_k2.ps1`, `scripts/smoke_test.ps1`, `scripts/eval_smoke.ps1`, and `scripts/capture_logs.ps1` exist in the working repo.
- `evals/README.md` documents the smoke eval contract.
- Engine startup emits `reone smoke signal: engine startup` after logging initializes.
- Smoke eval can verify build success, `engine.exe`, `shaderpack.erf`, the startup signal when launch is attempted, and absence of obvious fatal output.
- K1 and K2 smoke runs are attempted only against legal local game directories or explicitly skipped with `-AllowMissingGame`.
- No combat, boarding-party hostility, first-door, party, save/load, item, or modernization behavior changes are included.

Verification:

- Build the engine target.
- Build or verify the shader pack via the smoke harness.
- Run generic smoke eval with `-AllowMissingGame` if legal game paths are unavailable.
- Run K1 and K2 smoke/eval if legal install paths are available.

Non-goals:

- Do not port donor combat or hostility patches.
- Do not port donor Endar Spire first-door fixes in this milestone.
- Do not port donor developer overlay rendering or hotkey panels in this milestone.
- Do not modernize build, runtime, input, UI, rendering, or gameplay systems.

## Tiny Migration Milestone: Developer Overlay Observability

Goal: add the smallest useful in-game developer overlay and hotkeys for runtime observability without changing gameplay behavior.

Acceptance criteria:

- Developer overlay is gated by existing developer mode.
- `Ctrl+Shift+D` toggles the overlay.
- `Ctrl+Shift+W` toggles the read-only watch panel.
- Watch panel reads only existing state: screen, module, area, camera, speed, pause, relative mouse, leader, selected object, and hovered object.
- Overlay is off by default and can be disabled by hiding it or launching without developer mode.
- No donor trigger debug state, actor labels, target inspector, event feed, gameplay instrumentation, combat, hostility, boarding-party, first-door, item, party, cutscene, or modernization changes are included.

Verification:

- Build the engine target.
- Run generic smoke/eval with `-AllowMissingGame`.
- Run K1 smoke/eval.
- Run K2 smoke/eval.
- Human in-game verification should confirm `Ctrl+Shift+D` and `Ctrl+Shift+W` render and hide the overlay while developer mode is enabled.

Non-goals:

- Do not port donor trigger overlays in this milestone.
- Do not port donor actor labels or target inspector in this milestone.
- Do not port donor developer event feed or instrumentation in this milestone.
- Do not port donor gameplay behavior or modernize runtime systems.

## Tiny Migration Milestone: Trigger Zone Overlay

Goal: restore developer-mode trigger zone visualization as the next smallest high-leverage observability feature.

Acceptance criteria:

- Trigger visualization is gated by existing developer mode.
- `Ctrl+Shift+T` toggles trigger zone visualization.
- Trigger polygons are rendered as screen-space outlines inside the existing developer overlay.
- Trigger labels show read-only id, tag, and `OnEnter` script when available.
- The implementation reads existing trigger geometry only and does not add trigger occupancy, debug-state timers, script instrumentation, or gameplay behavior changes.
- The feature can be disabled by launching without developer mode, hiding the overlay with `Ctrl+Shift+D`, or hiding triggers with `Ctrl+Shift+T`.

Verification:

- Build the engine target.
- Run generic smoke/eval with `-AllowMissingGame`.
- Run K1 smoke/eval.
- Run K2 smoke/eval.
- Human in-game verification should confirm `Ctrl+Shift+T` shows and hides trigger outlines and labels while developer mode is enabled.

Non-goals:

- Do not port donor trigger debug state or occupancy instrumentation in this milestone.
- Do not port donor entity/world labels in this milestone.
- Do not port launcher developer-option UI changes in this milestone.
- Do not port gameplay, combat, hostility, boarding-party, encounter, first-door, item, party, cutscene, or modernization changes.

## Tiny Migration Milestone: Overlay Debug Labels And Trigger State

Goal: close the manually observed overlay gaps by restoring trigger state coloring and lightweight entity labels without changing gameplay behavior.

Acceptance criteria:

- Developer overlay remains gated by existing developer mode.
- `Ctrl+Shift+T` trigger zones show default/tested/inside/enter state in color and label text.
- `Ctrl+Shift+A` toggles lightweight entity labels over nearby creatures, doors, and placeables.
- Labels are read-only and limited to existing object state: id, tag, template, faction, hostile-to-player flag for creatures, selectable, commandable, visible, and plot.
- No donor target inspector, event feed, first-door diagnostics, combat, hostility, boarding-party, encounter, item, party, cutscene, launcher redesign, or modernization changes are included.

Verification:

- Build the engine target.
- Run generic smoke/eval with `-AllowMissingGame`.
- Run K1 smoke/eval.
- Run K2 smoke/eval.
- Human in-game verification should confirm `Ctrl+Shift+A` shows and hides entity labels, and trigger zones change from default/tested to inside/enter when crossed.

Non-goals:

- Do not port donor target inspector or event feed in this milestone.
- Do not port donor launcher developer-option controls in this milestone.
- Do not port donor gameplay behavior or modernize runtime systems.

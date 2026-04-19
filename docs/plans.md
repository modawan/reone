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

## Tiny Migration Milestone: K1 Trigger-Owned Delayed Actions

Goal: restore the old K1 early Carth/Trask handoff parity fix by allowing delayed actions queued on trigger objects to execute.

Acceptance criteria:

- `Trigger::update(float dt)` runs base `Object::update(dt)` before trigger tenant maintenance.
- The change is limited to trigger-owned base action/effect processing and does not alter trigger containment geometry, script dispatch arguments, combat legality, hostility, boarding-party persistence, encounter sequencing, or Carth content.
- Donor logging/eval instrumentation is not ported unless needed to prove a blocker.
- K1 and K2 smoke/eval pass after the change.

Verification:

- Build the engine target.
- Run generic smoke/eval with `-AllowMissingGame`.
- Run K1 smoke/eval.
- Run K2 smoke/eval.
- Human K1 verification should reach the early Endar Spire first-room/Trask-door/Carth handoff band and confirm the previously missing Carth-related cutscene/message progression occurs.

Non-goals:

- Do not force any first-Sith actor hostile.
- Do not port donor combat, reciprocal hostility, boarding-party, encounter, journal, Carth content, launcher, or modernization changes.
- Do not add broad diagnostics unless this minimal port fails validation or cannot be isolated.

## Tiny Migration Milestone: K1 Trask Auto-Dialogue Dispatch

Goal: restore the early Endar Spire scripted Trask follow-up conversations by fixing shared conversation fallback and assigned-action continuation semantics.

Acceptance criteria:

- `ActionStartConversation` calls with an empty dialogue resref can fall back to the target object's default conversation.
- `Object::clearAllActions(false)` preserves the active assigned action frame and queued continuation behind it when invoked from that active frame.
- The changes are limited to shared script/action semantics and do not alter Trask content, trigger geometry, combat legality, hostility, boarding-party persistence, encounter sequencing, item behavior, or cutscene logic.
- The previously integrated trigger-owned delayed-action fix remains unchanged.
- K1 and K2 smoke/eval pass after the change.

Verification:

- Build the engine target.
- Run generic smoke/eval with `-AllowMissingGame`.
- Run K1 smoke/eval.
- Run K2 smoke/eval.
- Human K1 verification should reach the Trask join and first-door handoff band and confirm both scripted follow-up dialogues auto-play without manual Trask conversation.

Non-goals:

- Do not port donor party-selection forced-companion work in this milestone.
- Do not port donor combat, reciprocal hostility, boarding-party, encounter, journal, Carth content, launcher, or modernization changes.
- Do not add broad diagnostics unless this minimal dispatch fix fails validation or cannot be isolated.

## Tiny Migration Milestone: Action Continuation-Safe ClearAllActions

Goal: preserve active assigned action continuations when a queued script action calls `ClearAllActions()`.

Acceptance criteria:

- `Object::clearAllActions(false)` no longer erases the currently executing action frame or continuation actions behind it when invoked from that active action.
- Forced clears and clears outside active action execution retain existing behavior.
- The change is generic action queue semantics, not Trask-specific content logic.
- K1 and K2 smoke/eval pass after the change.

Verification:

- Build the engine target.
- Run generic smoke/eval with `-AllowMissingGame`.
- Run K1 smoke/eval.
- Run K2 smoke/eval.
- Human K1 verification should close party selection after Trask joins and confirm the unlock-door dialogue auto-plays without temporary trace logging.

Non-goals:

- Do not port donor party-selection forced-companion work.
- Do not port combat, hostility, boarding-party, encounter, journal, content, launcher, or modernization changes.
- Do not turn temporary Trask trace logging into a permanent broad logging system.

## Tiny Migration Milestone: Stable Dev Tools Cleanup

Goal: remove temporary Trask tracing and keep developer tools on the existing launcher Developer Mode path.

Acceptance criteria:

- Temporary `reone trask autodialog trace:` log points are removed after live validation.
- The temporary party-selection caller helper is removed because the assigned action continuation fix is causal and `DoCommandAction` supplies the assigned actor as caller.
- The launcher Developer Mode area documents the current dev-tool hotkeys without adding a parallel config path.
- Trigger, dialogue fallback, and action continuation fixes remain intact.
- K1 and K2 smoke/eval pass after the cleanup.

Verification:

- Build engine and launcher targets.
- Run generic smoke/eval with `-AllowMissingGame`.
- Run K1 smoke/eval.
- Run K2 smoke/eval.
- Human verification should launch through `launcher.exe`, check Developer Mode, confirm the hotkey help is visible, and confirm `Ctrl+Shift+D/T/A/W` still work in game.

Non-goals:

- Do not add gameplay fixes, donor gameplay behavior, combat/hostility/boarding-party changes, broad launcher redesign, Dear ImGui work, or modernization.

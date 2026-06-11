# Residual handoff — modawan/reone#167

**Recorded:** 2026-06-05 (updated 2026-06-10 — LFG pass 21)  
**OpenKotOR `glad-gles`:** `00956ec4`  
**OpenKotOR `master`:** `dab7c08e`

## Blocker

Agent account `th3w1zard1` has **READ** on `modawan/reone` and cannot execute `MergePullRequest`. Downstream sync requires a modawan maintainer.

## PR state (verified)

| Field | Value |
|-------|--------|
| PR | https://github.com/modawan/reone/pull/167 |
| Head | `OpenKotOR:glad-gles` @ `00956ec4` |
| Base | modawan `master` @ `48b2ea3e` |
| GitHub | **MERGEABLE** (conflicts pre-resolved; `runOnLoadScript` + web traces in `game.cpp`) |
| CI | OpenKotOR `master` green @ `dab7c08e` (Linux/Windows build, gles-linux, wasm-ci); modawan #167 has no required checks |

## Maintainer merge steps

1. Open https://github.com/modawan/reone/pull/167
2. Confirm **Mergeable** (green) and review diff (~63 commits vs fork `master`)
3. **Squash merge** into modawan `master`
4. Verify modawan `master` advances past `48b2ea3e`

## Supersedes

- modawan/reone#163 — closed without merge
- modawan/reone#166 — closed; narrow #15 mirror

## After merge

- Update `tools/web/progress.md` to mark #167 merged
- Optional: delete or archive `glad-gles` on both remotes if fork tracks OpenKotOR `master` directly

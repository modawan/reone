# Residual handoff — modawan/reone#167

**Recorded:** 2026-06-05 (updated 2026-06-10)  
**OpenKotOR `glad-gles`:** `951f5c16`  
**OpenKotOR `master`:** `dc1a1eb5`

## Blocker

Agent account `th3w1zard1` has **READ** on `modawan/reone` and cannot execute `MergePullRequest`. Downstream sync requires a modawan maintainer.

## PR state (verified)

| Field | Value |
|-------|--------|
| PR | https://github.com/modawan/reone/pull/167 |
| Head | `OpenKotOR:glad-gles` @ `951f5c16` |
| Base | modawan `master` @ `48b2ea3e` |
| GitHub | **MERGEABLE** (conflicts pre-resolved; `runOnLoadScript` + web traces in `game.cpp`) |
| CI | OpenKotOR `master` green @ `dc1a1eb5`; `glad-gles` gles-linux @ `951f5c16` |

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

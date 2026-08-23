# GUI capture proof

GUI layout, scaling, list-spacing, splash-screen and movie presentation changes
need visual evidence. A build and a green test run are necessary but do not show
what the screen looks like.

`scripts/capture-gui-proof.ps1` renders the proof matrix headlessly and writes
one PNG per state into a folder. It needs a Release build in `build/bin` and
`ffmpeg` on PATH.

```powershell
.\scripts\capture-gui-proof.ps1 `
    -Kotor1Dir "<kotor install>" `
    -Kotor2Dir "<kotor 2 install>"
```

Output goes to `build/gui-proof/` as `<game>-<state>-<width>x<height>.png`.
`-States inventory,equipment-items` and `-Widths 3440` narrow a rerun while
iterating.

Before trusting a comparison between two builds, first prove that the selected
matrix reproduces against the same build:

```powershell
.\scripts\capture-gui-proof.ps1 `
    -Kotor1Dir "<kotor install>" `
    -Kotor2Dir "<kotor 2 install>" `
    -NoWorld `
    -VerifyReproducibility
```

`-VerifyReproducibility` captures each selected game-and-resolution batch twice
with identical commands and compares corresponding decoded RGBA pixels exactly.
It has no effect when omitted. If any captures differ, the harness checks the
entire selected matrix and then fails with every affected game, state and
resolution, including the maximum channel delta and the numbers of differing
pixels and channels. The ordinary output folder receives the second pass; the
first pass exists only under the temporary runtime folder used for comparison.

By default the scene is on. Pass `-NoWorld` when comparing captures between
builds: it emits `graphics off` so world rendering does not enter the
comparison. In that mode the harness also emits `seed 1337` before every state.
Each game-and-resolution batch runs in one engine process with one random
generator, so seeding only once per run lets drift accumulate from earlier
states. The visible symptom can be two builds disagreeing on the contents of a
list while every frame, baseline, margin and alignment axis still matches to
the pixel. Reseeding per state prevents that earlier-state exposure; it does not
identify the ultimate source of any irreproducibility.

The matrix covers both games at 1024x768 and 3440x1440: startup movie frames,
main menu, every character-generation window, the gameplay HUD and populated
combat action sequence, an area-transition banner, Swoop, the three Pazaak
screens, every in-game tab, a
wrapping bark bubble, an icon-bearing confirmation popup, dialogue and a
computer terminal. A dialogue whose replies overflow the bottom band shows the
reply list's scroll bar, and a TSL party-selection roster puts Handmaiden in
the slot she shares with Disciple while Mira and Hanharr are both away. A combined-overlay state intentionally presents the combat
HUD, bark bubble and confirmation popup at once to catch layering or shared-state
regressions. Every capture uses the default GUI presentation scales, including
the 50% list-row density. The matrix does not add alternate scale variants; use
a focused manual run when testing a non-default option.

Captures are driven by console commands, so any of them can also be run by hand:
`pause <frames>`, `capture <path> [frames]` and `quit` sequence a batch from a
`--commands-file`, while `skipmovie`, `openchargen`, `showhud`, `showbark`,
`showpopup`, `showtransition` and `showgallerymode` set up states that are
otherwise hard to reach deterministically.

When reviewing, zoom in rather than trusting a fitted thumbnail, and check that
movies stay inside the shorter viewport axis, menu and gameplay plates cover the
framebuffer, controls stay within the authored canvas, and list rows stay
registered with their icon and slot art from the first row to the last. A state
that fails to capture is a failed run, not a state to omit.

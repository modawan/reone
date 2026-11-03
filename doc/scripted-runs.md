# Scripted runs

`--commands-file FILE` runs console commands, one per line, as the engine
renders. Combined with `--headless 1` it plays a fixed sequence without anyone
at the keyboard and writes screenshots, which is how behaviour can be checked
against the original game without a display.

## Driving the game like a player

Two commands send real input, through the same path as the mouse and keyboard:

- `click <control tag>` moves the pointer to the centre of the named control
  on the current screen, then presses and releases the left button. It fails,
  and says why, when the click would not reach that control: no control has
  the tag, the hit test at its centre picks something else (it is hidden,
  disabled or not selectable, or another control covers it), or a movie,
  the credits or blocked player input would take the click. While a
  confirmation popup (a tutorial window, for one) is up, the tag is looked
  up in the popup, which takes every click: `click BTN_OK` dismisses it.
  Character generation and the in-game menu look up the tag in their front
  panel (the current step, the active tab) first, and the HUD in its status
  summary ("Journal Entry Added" and the like) while that is shown, then its
  pause indicator. The save screen's confirmation and name entry are not
  looked through.
- `key <name>` presses and releases a key: a single character (`a`, `1`) or
  a name such as `escape`, `return`, `space`, `up`, `f1`.

Every other console command (`warp`, `startconversation`, `selectobjectbytag`,
...) is also available. A command that fails is logged as
`Command failed: <command>: <reason>`, and a mistyped one as
`Unrecognized command: <name>`, so a scripted run leaves a trace in
`engine.log`. A click that quits the game ends the run.

`pause N` waits N rendered frames, not seconds. Headless with `--vsync 0`
renders hundreds of frames a second, so allow generous counts.

## Example: a new game to Trask's first line

```
graphics on
pause 310
click BTN_NEWGAME
pause 90
click BTN_SEL1
pause 60
click QUICK_CHAR_BTN
pause 60
click BTN_STEPNAME1
pause 60
click BTN_ACCEPT
pause 60
click BTN_STEPNAME2
pause 60
click BTN_RANDOM
click END_BTN
pause 60
click BTN_STEPNAME3
pause 120
key escape
pause 9000
capture trask.tga
quit
```

## Running headless on Linux

A machine with no display or sound device needs SDL's offscreen video driver
and OpenAL Soft's null backend:

```sh
SDL_VIDEO_DRIVER=offscreen ALSOFT_DRIVERS=null \
  engine --game /path/to/swkotor --commands-file commands.txt \
    --width 1024 --height 768 --winscale 100 --fullscreen 0 \
    --headless 1 --dev 0 --vsync 0 --pbr 0
```

Run it from a directory that holds `shaderpack.erf`; `engine.log` and
captures are written relative to it.

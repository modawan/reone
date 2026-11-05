# Scripted runs

`--commands-file FILE` runs console commands, one per line, as the engine
renders. Combined with `--headless 1` it plays a fixed sequence without anyone
at the keyboard and writes screenshots, which is how behaviour can be checked
against the original game without a display.

## Driving the game like a player

Two commands send real input, through the same path as the mouse and keyboard:

- `click <control tag> [row]` moves the pointer to the centre of the named
  control on the current screen, or with a row number to the middle of that
  shown row of a list (`click LB_ITEMS 1` for the second row), then presses
  and releases the left button. It fails,
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
  a name such as `escape`, `return`, `space`, `up`, `f1`. `keydown <name>`
  and `keyup <name>` hold a key across frames, for one such as `a` or `d`
  that turns the camera while held: `keydown a`, `pause 40`, `keyup a`.
- `clickobject <tag> [nth]` clicks an object in the world: the centre of
  its model's bounds, projected through the active camera, or else the
  nearest point over the rest of its bounds where the game's own picking
  finds it, as a player clicks round someone standing in front of a door.
  It fails when the object is not on screen or something covers all of
  it, so turn the camera or move first. As in the game, the first click on
  an object selects it and the second uses it.

- `clickat <x> <y>` clicks a window point, for a control no tag names, such
  as one icon of the feat chain in character generation and level-up.
  Nothing checks what is under the point, so capture the screen first to
  choose it.

Every other console command (`warp`, `startconversation`, `selectobjectbytag`,
...) is also available. In a scripted run, what commands print goes to the
log as `console: ...` lines, so `selectobjectbytag end_trask` then `info`
logs a creature's position, faction, `hp=current/max`, `dead=0|1`, its
follow slot and follow point if it follows the leader, whether it is
commandable, and the type of the action it is doing. `listcreatures` logs
every creature in the area the same way, with `party=1` on party members,
to find who is still fighting. Objects that share a tag are told apart by
an index from 0: `selectobjectbytag tar03_vulkaptenc 2` selects the third. A command that fails is logged as
`Command failed: <command>: <reason>`, and a mistyped one as
`Unrecognized command: <name>`, so a scripted run leaves a trace in
`engine.log`. A click that quits the game ends the run.

`listactions` logs the action showing in each of the three slots above the
selected object (`listactions: 0=none 1=4118 2=none`), and `clickaction
<slot>` clicks that slot's icon, as a player picks Security on a locked door
after selecting it.

`walkto <x> <y> [max frames]` walks the party leader to a point of the area
with the keys a player would hold: A or D to turn the camera and W to run,
steering along the walkmesh pathfinder's route round walls and debris. The
script waits until the walk ends, and the log says how it ended:
`walkto x,y arrived at ...`, `interrupted` (a conversation or a menu took the
screen, as walking into a trigger often does), `is blocked` or `timed out`.
Coordinates come from the module's own data, such as its waypoints. The walk
logs at its start whether it follows a walkmesh route or, having none, goes
straight; a closed door is no route, so a point beyond one goes straight
into a wall. `canwalk <x> <y>` logs whether the walkmesh joins the leader to
a point at all, to find the way before walking it. Doors are not part of
the walkmesh, so canwalk answers as if every door were open. `face <x> <y>`
turns the camera with A/D until it faces a point, without moving, so an
object behind the camera can be clicked; `info` on a door logs its
state, locked, keyrequired and plot flags. A sighting that pauses
the game ends a walk as `paused`; `unpause` then presses Space if, and only
if, the game is paused, since Space toggles the pause.

`savegame <slot> [name]` saves only from the in-game screen, as F4 does,
and `loadgame <n>` loads the nth save `listgames` lists, so a long run can
be split into stages that each start from the last one's save. A save in a
conversation is refused: the conversation would not be in it, and the
scripts on its remaining nodes, such as one that gives a companion back
its AI after a cutscene, would never run. Make sure a stage has finished
its fights before saving: what is fighting goes into the save, mid-swing.

`graphics off` stops rendering and with it the world: heartbeats, walks
and fights wait until `graphics on`.

`waitentry <n> [max frames]` waits until the conversation on show is at
entry n of its dialog and that entry has finished, so its replies are listed
and a `key` answers it. Entry numbers are the dialog file's EntryList
indices. A long dialog can then be scripted exactly, instead of timing key
presses: `waitentry 23`, `key 3`. An entry whose only reply is taken by
itself never lists replies, so wait for the entry after it instead.

`pause N` waits N rendered frames, not seconds. Headless with `--vsync 0`
renders hundreds of frames a second, so allow generous counts.

`waitscreen <screen> [max frames]` waits for the game instead: until it shows
the named screen (`ingame`, `conversation`, `container`, `chargen`, ... the
usage message lists them all), or for at most max frames (30000 by default),
after which `waitscreen <screen> timed out` is logged and the script goes on.
A conversation that starts when a creature has walked over, or a container
that opens when the player arrives, is reached reliably this way.

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

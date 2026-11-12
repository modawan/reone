# KotOR II main-menu presentation

K2 selects its menu scene from `GBL_MAIN_SITH_LORD`:

| Value | Model | Presentation |
| --- | --- | --- |
| 0 | `mainmenu01` | Sion |
| 1 | `mainmenu02` | Alternate Traya (unused by normal progression) |
| 2 | `mainmenu03` | Nihilus |
| 3 | `mainmenu04` | Traya |
| 4 | `mainmenu05` | Environment and current leader |
| Other or absent | `mainmenu01` | Sion fallback |

K1 retains its unnumbered `mainmenu`, orthographic projection, 1.4 framing
scale, and existing camera setup.

## Scene and lifetime

`MainMenu::refreshScene()` rebuilds K2's scene on GUI load and each subsequent
main-menu entry. Hosts that change the options snapshot can also call it
explicitly. It replaces only `kSceneMainMenu`, including its node arena and
render targets. Ordinary `SceneGraph::clear()` only removes roots and therefore
cannot release the old menu's cameras, emitters, and other allocated nodes.

Replacing a scene also drops the graphics context's framebuffer references
before their owners are destroyed. Screenshot readback invalidates the cached
read framebuffer when binding the window; otherwise the next scene blit can
incorrectly reuse that binding. A driver-free graphics test covers this
readback/blit sequence. No particle, shader, texture-unit, or uniform-buffer
behavior is changed.

K2 uses a 22.7259998-degree vertical perspective and near/far planes of
0.1/10000. Retail first inserts `gui3d_room` as a separate black enclosing
shell, then adds the selected numbered model at scale one. The room does not
parent or occlude the numbered model and supplies no menu camera or character
lighting. The numbered model supplies the authored floor, backdrop, lights,
continuous emitters, and looping `default` animation. Emitters use the shared
prewarming support; the menu does not alter their authored density or color.

The camera attaches to the `camerahook` directly below the authored scene root.
This matters for `mainmenu03`: it also contains a second `camerahook` inside
Nihilus, which the general duplicate-name lookup would select instead.

The dynamic leader uses `mainmenu05`'s `cutscenedummy` position (approximately
`(-1.01066, 0.905068, 0)` in the installed retail model), facing -Y. A model
without that placement hook falls back to `(0, -1, 0)`. The retail `evil`
animation is available through the PC supermodel chain; it loops on the body
and propagates to the head. Unsupported actors log a warning and request
`pause1` instead.

## Durable options

Before resetting a playable K2 session, `Game::openMainMenu()` copies the global
selector and, for value 4, the current leader's gender, appearance, effective
body column, and texture variation. Cold startup, character-generation cancel,
and partial-load recovery do not replace this snapshot with empty runtime state.
No save slot is inspected to choose the cold-start presentation.

The engine stores these top-level options in its existing `reone.cfg`:

```ini
k2-menu-selector=4
k2-menu-gender=0
k2-menu-appearance=136
k2-menu-body=1
k2-menu-texture=1
```

Body columns are zero-based (`a = 0`, `b = 1`, etc.). The writer replaces only
the five menu keys and retains unrelated lines, comments, and sections. A
complete valid leader tuple is required to insert the dynamic creature;
otherwise value 4 displays the environment alone.

The reconstructed actor comes from `Game::newPresentationCreature()`. It has
no gameplay registration, Party membership, saved identity, or retained live
leader reference. Body/texture overrides use the existing Creature model
builder; equipment objects and weapons are not copied. The tuple setter rejects
runtime creatures.

## Regression checks

Asset-free tests are in `test/game/mainmenu.cpp`:

```sh
build/bin/tests --gtest_filter='MenuPresentation.*:MainMenuTest.*'
```

The existing console command `openmenu main` exercises the same capture,
retirement, and refresh boundary as other main-menu entries. Headless engine
runs advance at 60 simulation frames per second, so `pause 1200` observes the
scene past one complete 16-second `default` animation cycle.

# The turret minigame

Notes behind `game/turret`. Everything here was read off the shipped
installations - `chitin.key`, the module archives, the `.are`/`.git`/`.ifo`
structures and the model animation tables - and off the authored module scripts.

## The authored encounters

`.are MiniGame.Type == 2` is a turret. Both installations together hold four:

| Game | Module | Enemies | Player HP | Tunnel | Music | Notes |
|---|---|---|---|---|---|---|
| K1 | `M12ab` | 6 | 3000 | pitch 2..45, yaw infinite | `mus_bat_sithbs` | Ebon Hawk turret, one asteroid obstacle |
| K2 | `421DXN` | 6 | 3000 | pitch 2..45, yaw infinite | none | Ebon Hawk turret, Dxun approach |
| K2 | `505OND` | 8 | 3000 | pitch 2..45, yaw infinite | none | Onderon ground turret |
| K2 | `107PER` | 0 | 1 | pitch -15..15, yaw -135..70 | none | Peragus hangar turret |

`211TEL`, `371NAR` and `510OND` are swoop races (`Type == 1`), not turrets.

The first three fly an authored rail and shoot minigame enemies. `107PER` has no
minigame enemies at all: it shoots the creatures its own area contains, and has
a section of its own below.

## Who does what

The turret flies the actors, aims and fires their guns, moves their bolts and
works out what each bolt hits. Everything else is the authored scripts':

* **Each actor is a script object.** The player, every enemy and every obstacle
  of the area layout is a `MinigameObject`, and its `.are` scripts run as it.
  The `SWMG_` routines those scripts call address the actors through these
  objects.
* **A hook with a script replaces the built-in handling**, which the script can
  still ask for: `OnDamage` is handed every change to the hit points and lands
  it with `SWMG_OnDamage()`, or does not; `OnDeath` carries a death out with
  `SWMG_OnDeath()`; `OnHitBullet` with `SWMG_OnBulletHit()`. The Ebon Hawk's
  damage scripts land a hit only while the hull is at or above 2000, which is
  what freezes its hit points just below that.
* **Heartbeats run sixty times a second**, whatever the frame rate, the
  player's first, then the enemies' and the obstacles'; a long frame catches up
  at most four. The player's keeps the cockpit: the compass from the aim's yaw,
  and the radar contacts once the wing starts moving.
* **The module's own scripts end the encounter**: the 114 second limit, the
  experience, the plot globals and the move to the next module are all script.
  The turret decides no win or loss.

### The order of arrival

A turret area's module load script runs first, then the turret comes up and its
actors' creation scripts run - the obstacles', the player's, then each
enemy's - then the module's client-enter and the area's enter scripts. The
authored scripts depend on it: `k_421_modload` zeroes `421DXN_Total_Enemies` and
each enemy's `k_421_enemygo` counts it back up, and in `107PER` the area's enter
script starts the cutscene over the turret already running.

A menu, conversation or cutscene that comes up over the turret returns to the
turret when it closes. The turret ends when its module is left.

### Animations an actor plays

`SWMG_PlayAnimation(actor, name, looping, queue, overlay)` plays on every model
of the actor; a model that does not author the animation is left alone. An
overlay stays on top and keeps running while what is beneath it is replaced -
so the radar keeps its contacts lit while its heading turns every update, and a
contact's removal pose (`SithLoopNNd`) shows over its live loop without the
loop being stopped. A queued play starts at once; no turret script queues one.

A hit plays the follower's `damage` once and goes back to `Ready_01`; a shot
plays the gun's `fire` once and goes back to `ready`. Custom animation events
(`custom_<tag>`) run the follower's `OnAnimEvent`, and a track that goes round
runs its `OnTrackLoop`.

### The scene around a turret

`PlayRoomAnimation(room, n)` plays the room's `scriptloopNN`, looping: over the
room's own loops in K1, in place of them in TSL. The turrets use it for their
backdrops - `k_pebo_skybox` picks which planet `m12ab_01a` shows from
`K_TURRET_SKYBOX`, and `a_roomanim` starts `421DXN_1`'s loop.

The area's `MiniGame.Music` loops for as long as the turret runs. A follower's
`Sounds.Engine` loops where the follower is while its track plays, at the
volume `SWMG_SetSoundVolume` gives it out of 127, which is none until a script
sets one; no turret script does.

## Shape of a flight encounter

* The player rides a `.lyt` track whose `track` animation walks a `modelhook`
  node. Models flagged `RotatingModel` take the aim; the rest stay in the track
  frame.
* Gun banks mount on `gunbankN` hooks and bolts leave from `bullethook0`.
* The camera mount model carries a `camerahook` node: `m12ab_camera`,
  `421dxn_2`, `505ond_mgh03`, `107perd`.
* The player's bolts take the heading of the player's first authored model in
  TSL, and of the gun in K1; in TSL a bolt also inherits the speed of the track
  it was fired from.
* An enemy gun turns toward the player's aim point within its horizontal and
  vertical spreads while the point lies within its sensing radius, and fires
  when it is on target and its time between shots has passed.
* A bolt hits what it crossed during the frame: in K1 a model's `hitbullet`
  part, in TSL the whole model. It explodes there with its `explode` animation,
  except on Onderon, where it just goes.

## Authored differences between the encounters

1. **The gun banks hang off a model that is not the first.** `m12ab` lists
   `mgf_turret` first and that model owns `gunbank0`/`gunbank1`; `421dxn`'s
   hooks are on `421dxn_11`, the third of six, and `505ond`'s on `505ond_tb01`,
   the third of three.
2. **The cockpit panes are not named alike.** `mgf_hud01`/`mgf_hud02`,
   `421dxn_h3`/`421dxn_h2`, `505ond_mgh01`/`505ond_mgh02`. The scripts play
   their animations on the player, and the pane that authors each one is the
   one that moves.
3. **`505ond_mgh02` authors only `Health00..Health10`.** The gauge arithmetic
   still reaches 13, so the top of the Onderon gauge has no animation to play.
4. **`505ond` has eight radar contacts**, `SithLoop02..SithLoop09`; `m12ab` and
   `421dxn` have six.
5. **The TSL damage scripts play a `hit` burst on the player**; K1's play none.
6. **`505ond` places no `Alarm01` sound object**, so it has no alarm.
7. **The TSL modules author a 114 second limit** in `k_421_modheart` and
   `k_505_modheart`; `m12ab` has none.
8. **The TSL modules award 100 experience per kill**; `m12ab` awards none.
9. **`421dxn` leaves from its damage script, `505ond` from its heartbeat.**
10. **K1 authors music and an obstacle**, `m12ab_mgo01`, an asteroid field with
    no scripts and nothing to collide with.
11. **The two games are tuned differently.** K1's fighters sense the player at
    200 units and fire every 0.4s for 10 damage; TSL's sense at 100 and fire
    every 1.0s for 25 (`421dxn`) or 20 (`505ond`).

## The authored end of a flight encounter

| | `m12ab` | `421dxn` | `505ond` |
|---|---|---|---|
| reset on load | `k_pebo_mgload` | `k_421_modload` | `k_505_modload` |
| counts enemies | `ebo_num_fighters` | `421DXN_Total_Enemies`, `421DXN_Dead_Enemies` | `505OND_Total_Enemies`, `505OND_Dead_Enemies` |
| completion flag | `ebo_turret_done` | `421DXN_Turret_Done` | `505OND_Turret_Done` |
| destruction flag | `M12AB_END_SYNC` | `421DXN_END_SYNC` | `505OND_END_SYNC` |
| time limit | none | 114s | 114s |
| experience | none | 100 x kills | 100 x kills |
| goes to | per the post-turret routing | `003EBO` | `504OND` at `From_505OND` |

The TSL limit is measured on the world clock; the authored `114.0` is 114 real
seconds.

K1 routes on plot state when the last fighter dies or the hawk is destroyed:
the HK-47 simulation returns to `ebo_m12aa` at `K_MINI_GAME`, a random
encounter returns there and moves on, and the escape from Taris returns at
`K_TARIS_DESTROYED` with a movie.

## 107PER, the hangar turret

The player mans the Ebon Hawk's turret while Sith troopers storm the Peragus
hangar bay. Its `.are` authors no enemies, one hit point, a bounded tunnel and
one gun bank at `Damage 300`, `Rate_Of_Fire 0.06`, `Speed 0.2` - a speed that
only carries because a bolt inherits the speed of the track it was fired from.

A bolt that meets none of the minigame's own actors stops where it crosses a
wall of the hangar, and otherwise strikes the first of the area's creatures
outside the party, or of its placeables, whose bounds it crosses. What it
strikes is sent a user-defined event numbered by the bolt's damage; the
troopers' `k_sith_damage` takes that much damage, so one bolt is one kill.

The wave is entirely scripted:

| step | script | what it does |
|---|---|---|
| area `OnEnter` | `a_106per_cut` | `107PER_MG_LEFT = 25`, `MG_DEAD = 0`, `MG_EBON = 0`, starts the `106percut` conversation |
| `106percut` | `a_106per_movie` | plays `permov04`, then starts the spawn chain |
| area `OnUserDefined` 1 | `a_hangar_timer` | a `g_sithtroop008` at `WP_TARGET_ENTER` every two seconds until 25 have come |
| trooper `OnDeath` | `a_sith_death` | `MG_DEAD++` |
| hangar trigger `OnEnter` | `a_sithexit` | a trooper that reaches the Hawk: `MG_EBON++` |
| area `OnUserDefined` 2 | `a_hangar_timer` | once `MG_DEAD + MG_EBON >= 25`: `MG_FINISHED = 1`, the `turend` conversation |
| `turend` | `a_load005ebo` | Atton and Kreia, opening on how many got aboard; the last reply starts `005EBO` |

The troopers follow their patrol across the bay and up the boarding ramp, so
the wave ends with some shot and the rest aboard whether or not the gun is
manned.

## Running an encounter

These are developer console commands, so the engine needs `--dev 1`; the console
key is backquote.

| command | what it does |
|---|---|
| `startturretgame <module>` | transition into a turret module, which starts its turret on arrival as a script transition does |
| `startturret` | start the current area's turret in place |
| `stopturret` | stop the turret and go back where `startturretgame` came from (Escape only pauses) |
| `turretstate` | print the aim, hit points, enemies, obstacles, bullets and track |
| `turretaim <pitch> <yaw>` | point the gun at an aim in degrees, kept inside the tunnel |
| `turretfire` | press the trigger once |
| `finishturret [count]` | deal that many enemies still flying, all by default, a hit that takes every hit point they have |
| `runscript <script> [Caller:<id>]` | run an authored script, as the object with that id |
| `info` | print the selected object's id, or the party leader's when nothing is selected |
| `listglobals` | print the global variables the scripts have written |

A turret area entered through a module transition starts its turret by itself;
`warp` loads a module directly and does not, so after a warp use `startturret`.

### Where the campaigns start each turret

Each of these runs the script the campaign runs at that point, from the module
it runs in. A script that delays its own module change - `a_load003ebo`,
`a_505turret` and `k_act_hk47simul` - needs an owner to carry the delayed
command: run `info`
first and pass the leader's id as `Caller:<id>`. Without one the delayed
command is refused and the screen stays faded out. A tutorial window that comes
up on arrival holds the world until it is closed.

| game | campaign moment | console |
|---|---|---|
| TSL | leaving the Peragus administration level for the hangar | `warp 106per`, then `runscript a_load003ebo Caller:<id>` |
| TSL | the Ebon Hawk heading for Onderon | `warp 003ebo`, then `runscript a_to_421dxn` |
| TSL | the Onderon turret | `warp 504ond`, then `runscript a_505turret Caller:<id>` |
| K1 | the escape from Taris | `warp ebo_m12aa`, then `runscript k_ren_taris03` |
| K1 | the escape from the Leviathan | `runscript k_ren_levescape` |
| K1 | leaving the Unknown World | `runscript k_ren_unkturret` |
| K1 | HK-47's turret simulation | `warp ebo_m12aa`, then `runscript k_act_hk47simul Caller:<id>` |
| K1 | a random attack between planets | the galaxy map's travel script, on a 50/50 roll |

The Taris, Leviathan and Unknown World exits depend on plot state the
campaign has set by then, so from the console those turrets run but do not
leave.

### By hand, in each encounter

**421DXN - Ebon Hawk over Dxun.** From `003EBO`, `startturretgame 421dxn`. The
Dxun backdrop animates. Each press of the trigger fires both barrels once,
parallel along the aim; holding it down fires no more. The compass
turns with the yaw; the six radar contacts light once the wing starts moving and
go out as fighters die; each hit steps the gauge down a band and plays a burst
on the hull, and the alarm starts at state 3. Bolts that hit explode on the
fighter. Clearing the wing, surviving 114 seconds, or being shot down all
return to `003EBO` with 100 experience per kill; `listglobals` then shows
`421DXN_Dead_Enemies` and `401DXN_STARTED = 4`.

**505OND - Onderon ground turret.** `startturretgame 505ond`. Eight contacts,
no alarm, no animation at the top of the gauge, and bolts vanish without
exploding. Every ending goes to `504OND` at `From_505OND`.

**M12AB - Ebon Hawk (K1).** In a K1 install, `warp ebo_m12aa` and then
`runscript k_act_hk47simul Caller:<id>`, HK-47's turret simulation, which sets
`K_HK47_SIMULATION` and starts `m12ab` itself. The battle music plays and the
backdrop shows the planet `K_TURRET_SKYBOX` names. Clearing the wing or being
destroyed both return to `ebo_m12aa` at `K_MINI_GAME`.

**107PER - Peragus hangar.** `startturretgame 107per`. The turret comes up with
the hangar live and the introductory cutscene plays over it. Troopers then
spawn at `WP_TARGET_ENTER` every two seconds, 25 of them, and cross the bay
toward the boarding ramp, which `turretaim 0 60` covers; one bolt drops a
trooper, bolts are stopped by the hangar's doors and walls, and the party is
never a target. Once every trooper is down or aboard, `turend` comes up and its
last reply leaves for `005EBO`; `listglobals` shows `107PER_MG_DEAD` and
`107PER_MG_EBON`.

To see a victory without playing it out, `finishturret`. To reach the 114 second
limit, thin the wing first - `finishturret 5` on Dxun leaves one fighter, which
cannot shoot the player down in time - then wait out the clock.

### Unattended

With a Release build, run from `build/bin` so `shaderpack.erf` and `reone.cfg`
are found:

```
engine.exe --game "<install>" --commands-file <file> --width 640 --height 360 \
           --dev 1 --logsev 0 --logch 1665
```

A commands file takes the console commands above plus `pause <frames>`,
`capture <path>.tga` and `quit`. Log channel 1665 adds the script and
conversation channels; the script channel logs every routine a script calls
with its arguments, which is where the authored globals, experience and module
moves can be read.

Run endings that depend on time in a window rather than with `--headless 1`.
Headless frames are a fixed sixtieth of a second while the world clock follows
real time, so the module heartbeat, the 114 second limit and every delayed
command fall behind the fighters. A windowed run takes each frame's real length,
so the two clocks agree as they do in play; the window must keep focus, since an
unfocused window holds the world still.

# Engine Options

Name | Description | Default
--- | --- | ---
game | path to game directory | current working directory
commands-file | execute console commands from a file at startup | empty
dev | enable developer mode | 0
width | render width | 1024
height | render height | 768
winscale | window scale in percents | 100
fullscreen | enable fullscreen | 0
headless | hide the window for scripted batch runs | 0
vsync | enable V-Sync | 1
wheelzoom | zoom the follow camera with the mouse wheel | 0
grass | enable grass | 1
shadows | enable shadows | 1
particles | enable emitter particles | 1
pbr | enable physically-based rendering | 0
ssao | enable screen-space ambient occlusion | 0
ssr | enable screen-space reflections | 0
fxaa | enable FXAA | 1
sharpen | enable image sharpening | 1
texquality | texture quality | 0
shadowres | shadow map resolution, 2^(10+N) | 1
anisofilter | anisotropic filtering | 2
drawdist | object draw distance in meters | 64
guiscale | GUI layout scale | 1.0
guitextscale | GUI text scale | 0.5
guidialogtextscale | dialog text scale | 0.6
guiborderscale | GUI border scale | 1.0
guilistscale | GUI list row scale | 0.5
musicvol | music volume in percents | 85
voicevol | voice volume in percents | 85
soundvol | sound volume in percents | 85
movievol | movie volume in percents | 85
voices2d | number of 2D voices, half for streams | 24
voices3d | number of 3D voices | 16
logsev | minimum log severity | 1 
logch | log channel mask | 1

Options can be passed on the command line, e.g. `engine --game GAME_DIR --dev 1`, or set as `name=value` in `reone.cfg` in the current working directory. Command-line values take precedence. GUI scale values must be finite and greater than zero.

### Developer Mode

Developer mode facilitates debugging of the engine. Enabling developer mode has the following consequences:

- Warp button is enabled in the main menu
- First person camera can be toggled using the V key
- Game speed can be changed using "+" and "-" keys

### Texture Quality

- 0 - High
- 1 - Medium
- 2 - Low

### Anisotropic Filtering

- 0 - Off
- 1 - 2x
- 2 - 4x
- 3 - 8x
- 4 - 16x

### Log Severity

- 0 - Debug
- 1 - Info
- 2 - Warning
- 3 - Error
- 4 - None

### Log Channels

- 1 - General
- 2 - Resources
- 4 - Resources (verbose)
- 8 - Graphics
- 16 - Audio
- 32 - GUI
- 64 - Perception
- 128 - Conversation
- 256 - Combat
- 512 - Script
- 1024 - Script (verbose)
- 2048 - Script (very verbose)

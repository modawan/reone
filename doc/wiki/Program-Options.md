Name | Description | Default
--- | --- | ---
game | path to game directory | N/A
dev | enable developer mode | 0
width | window width | 1024
height | window height | 768
fullscreen | enable fullscreen | 0
vsync | enable V-Sync | 0
grass | enable grass | 1
ssao | enable screen-space ambient occlusion | 1
ssr | enable screen-space reflections | 1
fxaa | enable FXAA | 1
sharpen | enable image sharpening | 1
texquality | texture quality | 0
shadowres | shadow map resolution, 2^(10+N) | 1
anisofilter | anisotropic filtering | 2
drawdist | object draw distance in meters | 64
musicvol | music volume in percents | 85
voicevol | voice volume in percents | 85
soundvol | sound volume in percents | 85
movievol | movie volume in percents | 85
logsev | minimum log severity | 1 
logch | log channel mask | 1

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
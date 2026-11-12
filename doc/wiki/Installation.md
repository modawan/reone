Requirements:
* Compiler with C++17 support (VS2019, GCC, Clang, etc.)
* [CMake](https://cmake.org/)

Dependencies:
* [Boost](https://www.boost.org/) ≥ 1.65
  * Boost.Algorithm
  * Boost.Endian
  * Boost.Format
  * Boost.Functional
  * Boost.Program_options
* [SDL2](https://www.libsdl.org/download-2.0.php)
* [GLM](https://glm.g-truc.net/0.9.9/index.html)
* [GLEW](http://glew.sourceforge.net/)
* [OpenAL](https://github.com/kcat/openal-soft)
* [MAD](https://www.underbit.com/products/mad/)
* [FFmpeg](https://ffmpeg.org/download.html#releases)
* [TinyXML-2](https://github.com/leethomason/tinyxml2)
* [wxWidgets](https://www.wxwidgets.org/downloads/) ≥ 3.0
* [GoogleTest](https://github.com/google/googletest)

**Note**: RelWithDebInfo configuration is **much** faster than Debug, regardless of OS.

## Windows

1. Install Visual Studio 2019 or later
1. Install CMake
1. Install [vcpkg](https://github.com/microsoft/vcpkg)
1. Install dependencies: `./vcpkg install --triplet x64-windows boost-algorithm boost-endian boost-format boost-functional boost-program-options glm sdl2 glew openal-soft libmad ffmpeg wxwidgets gtest`
1. Generate project files: `cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=[path to vcpkg]\scripts\buildsystems\vcpkg.cmake`

## Linux

### Ubuntu / Debian

1. Install Clang or GCC
1. Install CMake
1. Install dependencies: `sudo apt install libboost-all-dev libsdl2-dev libglew-dev libopenal-dev libmad0-dev libavcodec-dev libavformat-dev libavutil-dev libswresample-dev libswscale-dev libwxgtk3.0-gtk3-dev libgmock-dev`
1. Generate Makefiles: `cmake -B build -S . -DCMAKE_BUILD_TYPE=RelWithDebInfo`

### Fedora

1. Install Clang or GCC
1. Install CMake
1. Install dependencies: `sudo dnf install boost-devel SDL2-devel glew-devel openal-soft-devel libmad-devel ffmpeg-devel wxGTK3-devel libatomic gmock-devel`
1. Generate Makefiles: `cmake -B build -S . -DCMAKE_BUILD_TYPE=RelWithDebInfo`

### openSUSE

Build from source or install [this](https://build.opensuse.org/package/show/home:mnhauke:games/reone) package.

From source:

1. Install Clang or GCC
1. Install CMake
1. Install dependencies: `sudo zypper in boost-devel libboost_program_options-devel libSDL2-devel glew-devel openal-soft-devel libmad-devel wxWidgets-3_0-devel gmock`
1. Generate Makefiles: `cmake -B build -S . -DCMAKE_BUILD_TYPE=RelWithDebInfo -DENABLE_VIDEO=OFF`

**Note**: FFmpeg packages in openSUSE do not support Bink Video, hence video playback must be disabled. As a workaround, compile FFmpeg from source and specify absolute library paths in `CMakeLists.txt`.

## MacOS

1. Install Xcode Command Line Tools
1. Install Homebrew
1. Install CMake
1. Install dependencies: `brew install boost sdl2 glew openal-soft mad ffmpeg wxwidgets googletest`
1. Generate Makefiles: `cmake -B build -S . -DCMAKE_BUILD_TYPE=RelWithDebInfo`
1. In case of OpenAL not being found, run: `export PKG_CONFIG_PATH=$PKG_CONFIG_PATH:/usr/local/Cellar/openal-soft/YOUR_OPENAL_VERSION/lib/pkgconfig`
1. In case of linking errors, add libraries to library search path
    1. `export LDFLAGS="$LDFLAGS -L/usr/local/Cellar/sdl2/YOUR_SDL2_VERSION/lib"`
    1. `export LDFLAGS="$LDFLAGS -L/usr/local/Cellar/openal-soft/YOUR_OPENAL_VERSION/lib"`
    1. `export LDFLAGS="$LDFLAGS -L/usr/local/Cellar/ffmpeg/YOUR_FFMPEG_VERSION/lib"`

## CMake Options

|Name|Description|Default Value|
|-|-|-|
|BUILD_DATAMINER|Build dataminer executable|ON
|BUILD_TOOLKIT|Build toolkit executable|ON
|BUILD_LAUNCHER|Build launcher executable|ON
|BUILD_TESTS|Build tests|ON
|ENABLE_VIDEO|Enable video playback|ON
|ENABLE_ASAN|Enable address sanitizer (MSVC, Clang)|OFF|
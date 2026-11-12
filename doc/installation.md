# Installation

Requirements:
* Compiler with C++17 support (VS2019, GCC, Clang, etc.)
* [CMake](https://cmake.org/) ≥ 3.16
* pkg-config on Linux and macOS
* OpenGL 4.0-capable graphics driver

Dependencies:
* [Boost](https://www.boost.org/) ≥ 1.75 (Boost.JSON was [added in 1.75](https://www.boost.org/library/1.75.0/json/))
  * Boost.Algorithm
  * Boost.Endian
  * Boost.Format
  * Boost.Functional
  * Boost.Exception
  * Boost.JSON
  * Boost.Program_options
* [SDL 3](https://www.libsdl.org/)
* [GLM](https://glm.g-truc.net/) (bundled in `extern/glm`)
* [glad](https://github.com/Dav1dde/glad) (bundled in `extern/glad`)
* [Dear ImGui](https://github.com/ocornut/imgui) (bundled in `extern/dearimgui`)
* [OpenAL](https://github.com/kcat/openal-soft)
* [MAD](https://www.underbit.com/products/mad/)
* [FFmpeg](https://ffmpeg.org/download.html#releases) (unless `ENABLE_MOVIE=OFF`)
* [wxWidgets](https://www.wxwidgets.org/downloads/) (for launcher and toolkit; CI uses 3.2 on Linux)
* [GoogleTest / GoogleMock](https://github.com/google/googletest) (unless `BUILD_TESTS=OFF`)

Bundled dependencies do not need to be installed separately. wxWidgets must include the core, base, adv, stc, gl and aui components. FFmpeg must include libavcodec, libavformat, libavutil, libswresample and libswscale, with Bink decoding support for game movies.

**Note**: RelWithDebInfo configuration is **much** faster than Debug, regardless of OS.

## Windows

1. Install Visual Studio 2019 or later
1. Install CMake
1. Install [vcpkg](https://github.com/microsoft/vcpkg)
1. Install dependencies: `./vcpkg install --triplet x64-windows boost-algorithm boost-endian boost-format boost-functional boost-exception boost-program-options boost-json sdl3 openal-soft libmad ffmpeg wxwidgets gtest`
1. Generate project files: `cmake -B build -S . -A x64 -DCMAKE_TOOLCHAIN_FILE=[path to vcpkg]\scripts\buildsystems\vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows`
1. Build: `cmake --build build --config RelWithDebInfo`

## Linux

### Ubuntu / Debian

1. Install Clang or GCC
1. Install CMake
1. Install dependencies: `sudo apt install pkg-config libboost-all-dev libsdl3-dev libgl-dev libopenal-dev libmad0-dev libavcodec-dev libavformat-dev libavutil-dev libswresample-dev libswscale-dev libwxgtk3.2-dev libgmock-dev`
1. Generate Makefiles: `cmake -B build -S . -DCMAKE_BUILD_TYPE=RelWithDebInfo`
1. Build: `cmake --build build`

**Note**: CI uses Ubuntu 25.10. Older distributions may require building SDL 3 or a newer Boost from source if the required versions are unavailable in their repositories.

### Fedora

1. Install Clang or GCC
1. Install CMake
1. Install dependencies: `sudo dnf install pkgconf-pkg-config boost-devel SDL3-devel mesa-libGL-devel openal-soft-devel libmad-devel ffmpeg-free-devel wxGTK-devel libatomic gmock-devel`
1. Generate Makefiles: `cmake -B build -S . -DCMAKE_BUILD_TYPE=RelWithDebInfo`
1. Build: `cmake --build build`

### openSUSE

Build from source. An [openSUSE package](https://build.opensuse.org/package/show/home:mnhauke:games/reone) is also maintained separately; its version and dependencies may differ from this checkout.

From source:

1. Install Clang or GCC
1. Install CMake
1. Install dependencies (Tumbleweed): `sudo zypper in pkg-config boost-devel libboost_program_options-devel libboost_json-devel SDL3-devel Mesa-libGL-devel openal-soft-devel libmad-devel wxWidgets-3_2-devel gmock`
1. Generate Makefiles: `cmake -B build -S . -DCMAKE_BUILD_TYPE=RelWithDebInfo -DENABLE_MOVIE=OFF`
1. Build: `cmake --build build`

**Note**: This configuration omits FFmpeg and disables movie playback. To enable it, install FFmpeg development packages with Bink decoding support and configure with `-DENABLE_MOVIE=ON`. For a custom FFmpeg installation, expose its `.pc` files through `PKG_CONFIG_PATH`.

## MacOS

1. Install Xcode Command Line Tools
1. Install Homebrew
1. Install CMake
1. Install dependencies: `brew install boost sdl3 openal-soft mad ffmpeg wxwidgets googletest pkg-config`
1. Generate Makefiles: `cmake -B build -S . -DCMAKE_BUILD_TYPE=RelWithDebInfo`
1. Build: `cmake --build build`

OpenAL Soft is discovered automatically using `brew --prefix openal-soft`; the system OpenAL framework is used as a fallback. If Homebrew packages are not found, add `-DCMAKE_PREFIX_PATH="$(brew --prefix)"` when configuring. For pkg-config lookup issues, run `export PKG_CONFIG_PATH="$(brew --prefix openal-soft)/lib/pkgconfig:$(brew --prefix ffmpeg)/lib/pkgconfig:$PKG_CONFIG_PATH"` before configuring again.

## CMake Options

|Name|Description|Default Value|
|-|-|-|
|BUILD_DATAMINER|Build dataminer executable|ON
|BUILD_TOOLKIT|Build toolkit executable|ON
|BUILD_LAUNCHER|Build launcher executable|ON
|BUILD_TESTS|Build tests|ON
|BUILD_GFF2JSON|Build gff2json executable|ON
|BUILD_UNERF|Build unerf executable|ON
|BUILD_NCSRUN|Build ncsrun executable|ON
|ENABLE_MOVIE|Enable movie playback|ON
|ENABLE_ASAN|Enable address sanitizer (MSVC, Clang)|OFF|

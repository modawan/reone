# reone

[![Build Status](https://github.com/modawan/reone/actions/workflows/build-test-nix.yml/badge.svg)](https://github.com/modawan/reone/actions/workflows/build-test-nix.yml)

reone is a free and open source game engine, capable of running Star Wars: Knights of the Old Republic and its sequel, The Sith Lords. We aim to provide an engine that can be used to:

- Play KotOR and TSL from beginning to end
- Create mods that drastically change the gameplay or improve the visuals

The project is at a mid-stage of development: most engine subsystems have already been implemented, while game mechanics are actively being worked on. See [open issues](https://github.com/modawan/reone/issues) to get a sense of the remaining work.

## Legal

reone adheres to clean-room design principles. We do not condone the use of decompilation - instead, we rely on publicly available information (e.g., file format specification provided by BioWare) and our own observations of the game and its resources.

We do not distribute copyrighted materials - instead, we require our users and developers to obtain a legal copy of the game.

Lastly, reone is explicitly a non-commercial project: we do not seek to gain profit, neither through donations nor other means.

## Installation

To install reone, either download a prebuilt executable from the [releases](https://github.com/modawan/reone/releases) page, or build it from source. Building it requires a modern C++ compiler and a set of free and open source libraries. See detailed instructions for your operating system [here](doc/installation.md).

reone is being developed and tested against unmodded Steam versions of both KotOR and TSL. Compatibility with any other version is not guaranteed, although GOG and retail versions should work with no or minor modification.

Install [Visual C++ Redistributable](https://aka.ms/vs/16/release/vc_redist.x64.exe) to run prebuilt Windows executables.

### Linux

Each release has two Linux downloads for x86_64: an AppImage, and a tarball of the same files for systems without AppImage support. They are built on AlmaLinux 8 for glibc 2.28 and newer (Debian 10, Ubuntu 18.10, RHEL 8, SteamOS and later), and every build is tested on AlmaLinux 8, Ubuntu 22.04 and 26.04, Debian 12, Fedora and Arch. Both need an OpenGL 4.0 driver and GTK 3, which every desktop distribution has.

```sh
chmod +x reone-*-x86_64.AppImage
./reone-*-x86_64.AppImage                          # the launcher
./reone-*-x86_64.AppImage --game /path/to/swkotor  # the engine
./reone-*-x86_64.AppImage toolkit                  # the toolkit
./reone-*-x86_64.AppImage --help                   # everything else

tar xf reone-*-linux-x86_64.tar.gz
reone-*-linux-x86_64/reone                         # same commands as the AppImage
```

Each release also lists the SHA-256 checksums of both files, and GitHub keeps a [build provenance attestation](https://docs.github.com/actions/security-for-github-actions/using-artifact-attestations) for them: `gh attestation verify reone-*-x86_64.AppImage --repo modawan/reone`.

`cmake --install` installs the same layout as the packages: `reone`, `reone-launcher` and `reone-toolkit` in `bin`, the applications they start in `libexec/reone`, and their data in `share/reone`, with desktop entries and icons.

### Nix

On x86_64 Linux with [Nix](https://nixos.org) (flakes enabled), the flake builds every application and runs the unit tests:

```sh
nix build                                  # entry points in result/bin
nix run . -- --game /path/to/swkotor       # run the engine
nix run .#launcher                         # run the launcher
nix run .#toolkit                          # run the toolkit
nix flake check                            # build, unit tests and install checks
```

`result/bin` holds `reone` (the engine), `reone-launcher`, `reone-toolkit`, and the command-line tools `dataminer`, `gff2json`, `unerf` and `ncsrun`.

On Linux distributions other than NixOS, Nix-built OpenGL programs cannot see the system GPU driver. Run the engine through [nixGL](https://github.com/nix-community/nixGL): `nix run --impure github:nix-community/nixGL -- result/bin/reone --game /path/to/swkotor`, or use the AppImage.

For development, `nix develop` opens a shell with the same compiler and libraries plus clangd and gdb, then build with CMake and Ninja:

```sh
nix develop
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
ctest --test-dir build --output-on-failure
```

The "Build and test with Nix" workflow's `linux` job builds and tests in the flake's dev shell with ccache, so CI and `nix develop` share one compiler and one set of libraries.

## Usage

Use launcher application to choose a game directory and launch reone. Alternatively, reone can be started from the command line: `engine --game GAME_DIR` (`engine.exe` on Windows, `reone --game GAME_DIR` for an installed or packaged reone on Linux). `engine --version` prints the version.

## Configuration

reone can be configured via the launcher, the configuration file or the command line.

The optional configuration file is named "reone.cfg". The launcher writes it and the engine reads it, and writes its log, "engine.log", and its debug window layout, "imgui.ini", beside it. It is kept in the current working directory if one is already there, which is also where it is kept on Windows and macOS. Otherwise, on Linux, it is kept in `~/.config/reone` (`$XDG_CONFIG_HOME/reone` when that is set). See complete list of engine options [here](doc/engine-options.md).

## Contributing

reone is open to contributors. Get in touch with our development team on [Discord](https://discord.gg/6sqSyfn8Jp) if you'd like to work on any of the [open issues](https://github.com/modawan/reone/issues).

Developer resources:

- [Project Architecture](doc/project-architecture.md)
- [Code Style](doc/code-style.md)
- [Testing](doc/testing.md)
- [Tooling](doc/tooling.md)
- [Learning Resources](doc/learning-resources.md)

## Similar Projects

reone is not the only project aiming to reimplement the Odyssey engine - there are at least four alternatives:

- [xoreos](https://github.com/xoreos/xoreos) - a reimplementation of BioWare's Aurora engine and its derivatives
- [KotOR.js](https://github.com/KobaltBlu/KotOR.js) - a remake of the Odyssey engine written in JavaScript
- [The Northern Lights](https://github.com/lachjames/NorthernLights) - a Unity-based reimplementation and a level editor
- [KotOR-Unity](https://github.com/rwc4301/KotOR-Unity) - another Unity-based reimplementation

In contrast, this is what reone offers:

- KotOR and TSL only, unlike xoreos, which aims to reimplement all of Aurora engine games - codebase is lighter and development is more focused
- Written in C++ - industry standard language, faster than the JavaScript implementation
- Custom game engine based on SDL 3 and OpenGL 4.0 - does not depend on Unity, which is a proprietary software with a learning curve of itself

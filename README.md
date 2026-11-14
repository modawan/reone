# reone

[![Build Status](https://github.com/modawan/reone/actions/workflows/build-test-linux.yml/badge.svg)](https://github.com/modawan/reone/actions/workflows/build-test-linux.yml)

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

### Nix

On x86_64 Linux or an Apple silicon Mac with [Nix](https://nixos.org) (flakes enabled), the flake builds every application and runs the unit tests:

```sh
nix build                                  # entry points in result/bin
nix run . -- --game /path/to/swkotor       # run the engine
nix run .#launcher                         # run the launcher
nix run .#toolkit                          # run the toolkit
nix flake check                            # build, unit tests, install and wrapper checks
```

`result/bin` holds `reone` (the engine), `reone-launcher`, `reone-toolkit`, and the command-line tools `dataminer`, `gff2json`, `unerf` and `ncsrun`.

The engine, launcher and toolkit read their files from the current directory, so `reone`, `reone-launcher` and `reone-toolkit` run them inside `$REONE_HOME`. That defaults to `$XDG_DATA_HOME/reone`, or `~/.local/share/reone` when `XDG_DATA_HOME` is unset, and is created on first run. It holds your `reone.cfg` and `engine.log`, plus links into the Nix store. Saves are not kept there: the engine keeps them in the `saves` directory of the game. A relative `--game` path is resolved from where you ran the command, and with no `--game` and no `game=` in `reone.cfg`, the current directory is used, as upstream does.

On Linux distributions other than NixOS, Nix-built OpenGL programs cannot see the system GPU driver. Run the engine through [nixGL](https://github.com/nix-community/nixGL): `nix run --impure github:nix-community/nixGL -- result/bin/reone --game /path/to/swkotor`.

For development, `nix develop` opens a shell with the same compiler and libraries plus clangd and a debugger (gdb on Linux; on macOS lldb, which uses the debugserver from Xcode or the Command Line Tools), then build with CMake and Ninja:

```sh
nix develop
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
ctest --test-dir build --output-on-failure
```

## Usage

Use launcher application to choose a game directory and launch reone. Alternatively, reone can be started from the command line: `engine --game GAME_DIR` (`engine.exe` on Windows).

## Configuration

reone can be configured via the launcher, the configuration file or the command line.

The optional configuration file, named "reone.cfg", is read from the current working directory. See complete list of engine options [here](doc/engine-options.md).

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

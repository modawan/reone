# reone for arm64 macOS, built on x86_64 Linux.
#
# nixpkgs cannot cross-compile to Darwin from Linux (its cctools, ld64 and
# apple-sdk packages are darwin-only), so this is a separate toolchain: zig's
# bundled clang and its own Mach-O linker produce the objects and binaries,
# the macOS SDK (nixpkgs' own download of it, served by cache.nixos.org)
# provides the framework headers and link stubs, and every library reone links
# (FFmpeg included, cut down to the game's Bink movies) is built here,
# statically, with the same toolchain. The result is an engine that runs on
# Apple silicon Macs; nothing here can run it on the build machine.
#
# The SDK's licence: Apple's Xcode and Apple SDKs Agreement grants its
# installation on Apple-branded computers only (section 2.2.A) and forbids
# using it on any other computer, or enabling others to (sections 2.5 and
# 2.7). Every build with it on Linux is outside that licence, the CI
# workflow's included. osxcross works the same way and says so in its README;
# nixpkgs does not (its apple-sdk is darwin-only, only its download is reused
# here). Removing .github/workflows/build-macos-cross.yml stops CI doing it.
{
  pkgs,
  version,
  # nix/shaderpack.nix: shaderpack.erf built natively (platform-independent
  # data that only the freshly built shaderpack tool can produce, and that
  # tool is a Mach-O binary in the cross build) goes next to the engine.
  shaderpack,
}:

let
  inherit (pkgs) lib;

  sdk = pkgs.callPackage ./sdk.nix { };
  toolchain = pkgs.callPackage ./toolchain.nix { inherit sdk; };
  cmakeToolchainFile = "${toolchain}/${toolchain.cmakeToolchainFile}";

  # A derivation built with the cross toolchain: the host tools on PATH, the
  # toolchain file on every cmake call (dependencies through CMAKE_PREFIX_PATH
  # from `crossInputs`), ninja for building and installing. nixpkgs' cmake
  # hook is bypassed because, without a host compiler in the stdenv, it would
  # pass empty CMAKE_C_COMPILER and CMAKE_AR values that fight the toolchain.
  # zig writes a cache (its compiler-rt and libc++ for the target); the
  # sandbox has no home, so it goes to the build's temporary directory.
  # meta.platforms names the build machine (Linux, where boost.nix and
  # reone.nix set it; the other recipes inherit nixpkgs' meta, which includes
  # it): Nix checks it against the system doing the building, and nothing
  # tells it that the output is for another one.
  mkCross =
    attrs:
    pkgs.stdenvNoCC.mkDerivation (
      {
        strictDeps = true;
        dontUseCmakeConfigure = true;
        # The fixup phase would run the build machine's strip over the
        # outputs when one is on the path (ffmpeg.nix brings binutils' with
        # its host compiler): GNU strip cannot read Mach-O members but
        # rewrites a darwin archive into GNU format with the symbol table
        # left as an ordinary member, which zig's linker then takes for an
        # object ("unknown cpu architecture").
        dontStrip = true;
        crossInputs = [ ];
        cmakeFlags = [ ];

        # After unpacking, so every later phase has it (libmad skips configure).
        postUnpack = ''
          export ZIG_GLOBAL_CACHE_DIR=$TMPDIR/zig-global-cache
          export ZIG_LOCAL_CACHE_DIR=$TMPDIR/zig-local-cache
        '';

        configurePhase = ''
          runHook preConfigure
          prefixes=$(printf '%s;' $crossInputs)
          cmake -S . -B build -G Ninja \
            -DCMAKE_TOOLCHAIN_FILE=${cmakeToolchainFile} \
            -DCMAKE_BUILD_TYPE=Release \
            -DCMAKE_INSTALL_PREFIX=$out \
            "-DCMAKE_PREFIX_PATH=''${prefixes%;}" \
            $cmakeFlags "''${cmakeFlagsArray[@]}"
          cd build
          runHook postConfigure
        '';
      }
      // attrs
      // {
        nativeBuildInputs = [
          toolchain
          pkgs.cmake
          pkgs.ninja
          pkgs.pkg-config
          pkgs.llvm
        ]
        ++ (attrs.nativeBuildInputs or [ ]);
      }
    );

  callCross = path: extra: pkgs.callPackage path ({ inherit mkCross; } // extra);

  deps = {
    sdl3 = callCross ./sdl3.nix { };
    boost = callCross ./boost.nix { };
    openal-soft = callCross ./openal-soft.nix { };
    libmad = callCross ./libmad.nix { };
    ffmpeg = callCross ./ffmpeg.nix { };
    gtest = callCross ./gtest.nix { };
  };

  # One prefix with every dependency: lib/, include/, lib/cmake, lib/pkgconfig.
  depsPrefix = pkgs.symlinkJoin {
    name = "reone-aarch64-darwin-deps";
    paths = builtins.attrValues deps;
  };

  reone = callCross ./reone.nix {
    inherit version shaderpack;
    inherit (toolchain) deploymentTarget;
    deps = depsPrefix;
  };

  # The same toolchain and dependencies, for iterating with cmake/ninja by
  # hand. CMake reads CMAKE_TOOLCHAIN_FILE and CMAKE_PREFIX_PATH from the
  # environment, so a plain `cmake -B build/darwin -G Ninja` configures the
  # cross build. The `all` target cannot be built: create_shaderpack runs the
  # freshly built (Mach-O) shaderpack tool, so build the named targets and
  # take shaderpack.erf from the native reone-shaderpack package. The shell
  # does not depend on that package (a native build) so that entering it
  # stays cheap.
  shell = pkgs.mkShellNoCC {
    name = "reone-aarch64-darwin";
    packages = [
      toolchain
      pkgs.cmake
      pkgs.ninja
      pkgs.pkg-config
      pkgs.llvm
    ];
    # Only what CMake reads; the SDK path is baked into the toolchain, so the
    # shell and the package build the same way.
    env = {
      CMAKE_TOOLCHAIN_FILE = cmakeToolchainFile;
      CMAKE_PREFIX_PATH = depsPrefix;
    };
    shellHook = ''
      cat <<EOF
      reone cross shell: arm64 macOS binaries from this machine (zig + macOS SDK ${sdk.version} at ${sdk}).

        cmake -B build/darwin -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_LAUNCHER=OFF -DBUILD_TOOLKIT=OFF
        ninja -C build/darwin engine tests dataminer gff2json unerf ncsrun
        nix build .#reone-shaderpack -o result-shaderpack && cp result-shaderpack/share/reone/shaderpack.erf build/darwin/bin/

      Copy build/darwin/bin/engine and shaderpack.erf to one folder on a Mac to run them.
      EOF
    '';
  };
in
{
  inherit
    sdk
    toolchain
    deps
    depsPrefix
    reone
    shell
    ;
}

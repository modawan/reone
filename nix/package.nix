{
  lib,
  stdenv,
  cmake,
  ninja,
  pkg-config,
  wrapGAppsHook3,
  alsa-lib,
  boost,
  libGL,
  libmad,
  openal,
  sdl3,
  ffmpeg,
  wxwidgets_3_2,
  gtest,
  version ? "0-unstable",
}:

let
  # wrapGApp (Linux only, see postFixup) moves the real binary to
  # .<name>-wrapped and puts a shell script in its place.
  wxApp = name: if stdenv.hostPlatform.isLinux then ".${name}-wrapped" else name;
in
stdenv.mkDerivation {
  pname = "reone";
  inherit version;

  # Only what CMake reads. Keeps nix/, flake files and docs edits from
  # invalidating the build.
  src = lib.fileset.toSource {
    root = ../.;
    fileset = lib.fileset.unions [
      ../CMakeLists.txt
      ../cmake
      ../extern
      ../glsl
      ../include
      ../src
      ../test
      ../assets
      ../.clang-format
    ];
  };

  nativeBuildInputs = [
    cmake
    ninja
    pkg-config
  ]
  ++ lib.optionals stdenv.hostPlatform.isLinux [
    # Gives the wx apps their GSettings schemas and GTK modules. wx uses
    # Cocoa on macOS, where there is nothing to wrap.
    wrapGAppsHook3
  ];

  buildInputs = [
    boost
    ffmpeg
    libmad
    openal
    sdl3
    wxwidgets_3_2
    gtest
  ]
  ++ lib.optionals stdenv.hostPlatform.isLinux [
    # sdl3.pc names alsa in Requires.private; without it pkg-config prints
    # "Package 'alsa' not found" on every SDL3 lookup.
    alsa-lib
    # On macOS, find_package(OpenGL) finds the SDK framework instead.
    libGL
  ];

  # Upstream's defaults build every app, the tests and movie playback.
  cmakeFlags = [ (lib.cmakeBool "BUILD_TESTS" true) ];

  doCheck = true;
  # Upstream registers one ctest entry that runs the whole gtest binary.
  checkPhase = ''
    runHook preCheck
    ctest --output-on-failure
    runHook postCheck
  '';

  # The engine reads shaderpack.erf and reone.cfg from the current directory
  # and logs there; the launcher writes reone.cfg there and starts "./engine";
  # the toolkit reads cmudict.dict from there. None of that works from the
  # read-only store, so those three go to libexec, and bin/reone,
  # bin/reone-launcher and bin/reone-toolkit are links to reone-run, which
  # runs them from a per-user directory (see reone-run.sh). The CLI tools take
  # paths as arguments and stay in bin/ as they are.
  postInstall = ''
    install -Dm644 bin/shaderpack.erf $out/share/reone/shaderpack.erf
    if [ -e bin/cmudict.dict ]; then
      install -Dm644 bin/cmudict.dict $out/share/reone/cmudict.dict
    fi

    mkdir -p $out/libexec/reone
    for exe in engine launcher toolkit; do
      if [ -e $out/bin/$exe ]; then
        mv $out/bin/$exe $out/libexec/reone/$exe
      fi
    done

    install -Dm755 ${./reone-run.sh} $out/libexec/reone/reone-run
    substituteInPlace $out/libexec/reone/reone-run --subst-var-by out $out
    ln -s ../libexec/reone/reone-run $out/bin/reone
    for exe in launcher toolkit; do
      if [ -e $out/libexec/reone/$exe ]; then
        ln -s ../libexec/reone/reone-run $out/bin/reone-$exe
      fi
    done

    # Upstream installs its static libraries too; nothing outside this build
    # links them and there are no headers to go with them.
    rm -rf $out/lib
  '';

  # Only the wx apps need the GApps wrapper (GSettings schemas, GTK modules),
  # and only on Linux.
  dontWrapGApps = true;
  postFixup = lib.optionalString stdenv.hostPlatform.isLinux ''
    for exe in launcher toolkit; do
      if [ -e $out/libexec/reone/$exe ]; then
        wrapGApp $out/libexec/reone/$exe
      fi
    done
  '';

  # Guards the install layout. Upstream's install rule once installed no
  # applications at all (InstallTargets never left its subdirectory scope)
  # while the build and unit tests still passed, so check the result itself.
  # reone-run's own behaviour is tested by nix/reone-run-test.sh.
  doInstallCheck = true;
  installCheckPhase = ''
    runHook preInstallCheck

    expect=(bin/reone libexec/reone/engine share/reone/shaderpack.erf)
    expect+=(bin/reone-launcher libexec/reone/${wxApp "launcher"})
    expect+=(bin/reone-toolkit libexec/reone/${wxApp "toolkit"} share/reone/cmudict.dict)
    expect+=(bin/dataminer bin/gff2json bin/unerf bin/ncsrun)
    for f in "''${expect[@]}"; do
      [ -e "$out/$f" ] || { echo "install check: missing $f"; exit 1; }
    done

    # The engine must start from REONE_HOME: links set up, log written
    # there. SDL's dummy video driver is built on every platform and has no
    # OpenGL, so SDL_Init succeeds and window creation fails the same way on
    # Linux and macOS, without reaching for X11 or the macOS window server
    # from the build sandbox. A logged "Engine failure" then means the
    # wrapper ran the real binary, options parsed, and the engine shut down
    # cleanly instead of crashing.
    export REONE_HOME=$TMPDIR/reone-home
    export SDL_VIDEO_DRIVER=dummy
    status=0
    $out/bin/reone --game "$TMPDIR/no-game" || status=$?
    [ "$status" -ne 0 ] || { echo "install check: engine ran with no game data?"; exit 1; }
    for f in shaderpack.erf engine; do
      [ -L "$REONE_HOME/$f" ] || { echo "install check: REONE_HOME/$f not linked"; exit 1; }
    done
    grep -q "Engine failure" "$REONE_HOME/engine.log" \
      || { echo "install check: engine did not log a clean failure"; cat "$REONE_HOME/engine.log" || true; exit 1; }

    runHook postInstallCheck
  '';

  meta = {
    description = "Free and open source engine for Star Wars: KotOR and TSL";
    homepage = "https://github.com/modawan/reone";
    license = lib.licenses.gpl3Plus;
    # Not x86_64-darwin: nixpkgs unstable dropped it (see flake.nix).
    platforms = lib.platforms.linux ++ [ "aarch64-darwin" ];
    mainProgram = "reone";
  };
}

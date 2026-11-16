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
      ../packaging
      ../.clang-format
    ];
  };

  nativeBuildInputs = [
    cmake
    ninja
    pkg-config
    wrapGAppsHook3
  ];

  buildInputs = [
    # sdl3.pc names alsa in Requires.private; without it pkg-config prints
    # "Package 'alsa' not found" on every SDL3 lookup.
    alsa-lib
    boost
    ffmpeg
    libGL
    libmad
    openal
    sdl3
    wxwidgets_3_2
    gtest
  ];

  # Upstream's defaults build every app, the tests and movie playback. The
  # source has no .git, so the version comes from the flake.
  cmakeFlags = [
    (lib.cmakeBool "BUILD_TESTS" true)
    (lib.cmakeFeature "REONE_VERSION" version)
  ];

  doCheck = true;
  # Upstream registers one ctest entry that runs the whole gtest binary.
  checkPhase = ''
    runHook preCheck
    ctest --output-on-failure
    runHook postCheck
  '';

  # The applications find shaderpack.erf and cmudict.dict relative to
  # themselves and keep reone.cfg and engine.log in ~/.config/reone, so the
  # CMake install layout runs as it is: bin/reone, bin/reone-launcher and
  # bin/reone-toolkit link to libexec/reone, the data is in share/reone.
  postInstall = ''
    # Upstream installs nothing else under lib.
    rm -rf $out/lib
  '';

  # Only the wx apps need the GApps wrapper (GSettings schemas, GTK modules).
  dontWrapGApps = true;
  postFixup = ''
    for exe in launcher toolkit; do
      wrapGApp $out/libexec/reone/$exe
    done
  '';

  # Guards the install layout: upstream's install rule once installed no
  # applications at all (InstallTargets never left its subdirectory scope)
  # while the build and unit tests still passed, so check the result itself.
  doInstallCheck = true;
  installCheckPhase = ''
    runHook preInstallCheck

    expect=(bin/reone bin/reone-launcher bin/reone-toolkit)
    expect+=(libexec/reone/engine libexec/reone/.launcher-wrapped libexec/reone/.toolkit-wrapped)
    expect+=(share/reone/shaderpack.erf share/reone/cmudict.dict)
    expect+=(bin/dataminer bin/gff2json bin/unerf bin/ncsrun)
    expect+=(share/applications/io.github.modawan.reone.desktop share/metainfo/io.github.modawan.reone.metainfo.xml)
    expect+=(share/icons/hicolor/256x256/apps/io.github.modawan.reone.png)
    for f in "''${expect[@]}"; do
      [ -e "$out/$f" ] || { echo "install check: missing $f"; exit 1; }
    done

    $out/bin/reone --version | grep -qx "reone ${version}" \
      || { echo "install check: unexpected --version: $($out/bin/reone --version)"; exit 1; }

    # Started from a directory with nothing of reone's in it, the engine must
    # keep its log in ~/.config/reone. The build sandbox has no display, so it
    # fails before it gets to any game data; that it fails cleanly, logging
    # the failure, is checked too. (The Linux packages workflow runs it on
    # real distributions, where it gets as far as the game's key file.)
    export HOME=$TMPDIR/home
    unset XDG_CONFIG_HOME
    mkdir -p $TMPDIR/elsewhere $HOME
    status=0
    (cd $TMPDIR/elsewhere && $out/bin/reone --game "$TMPDIR/no-game") || status=$?
    [ "$status" -ne 0 ] || { echo "install check: engine ran with no game data?"; exit 1; }
    log=$HOME/.config/reone/engine.log
    [ -e "$log" ] || { echo "install check: no $log"; ls -la $TMPDIR/elsewhere; exit 1; }
    [ ! -e $TMPDIR/elsewhere/engine.log ] || { echo "install check: engine logged to the current directory"; exit 1; }
    grep -q "Engine failure" "$log" \
      || { echo "install check: engine did not log a clean failure"; cat "$log"; exit 1; }

    runHook postInstallCheck
  '';

  meta = {
    description = "Free and open source engine for Star Wars: KotOR and TSL";
    homepage = "https://github.com/modawan/reone";
    license = lib.licenses.gpl3Plus;
    platforms = lib.platforms.linux;
    mainProgram = "reone";
  };
}

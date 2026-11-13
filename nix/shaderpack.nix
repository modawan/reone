# shaderpack.erf, the engine's packed GLSL shaders, built natively: only the
# shaderpack tool and the libraries it links, then the create_shaderpack
# target that runs it. The macOS cross build cannot run the tool it builds
# (a Mach-O binary), and the data it produces is platform-independent, so it
# takes this one. A fraction of the full package's build.
{
  lib,
  stdenv,
  cmake,
  ninja,
  pkg-config,
  alsa-lib,
  boost,
  libGL,
  libmad,
  openal,
  sdl3,
  version ? "0-unstable",
}:

stdenv.mkDerivation {
  pname = "reone-shaderpack";
  inherit version;

  src = import ./source.nix { inherit lib; };

  nativeBuildInputs = [
    cmake
    ninja
    pkg-config
  ];

  buildInputs = [
    alsa-lib
    boost
    libGL
    libmad
    openal
    sdl3
  ];

  cmakeFlags = [
    (lib.cmakeBool "BUILD_TESTS" false)
    (lib.cmakeBool "BUILD_LAUNCHER" false)
    (lib.cmakeBool "BUILD_TOOLKIT" false)
    (lib.cmakeBool "BUILD_DATAMINER" false)
    (lib.cmakeBool "BUILD_GFF2JSON" false)
    (lib.cmakeBool "BUILD_UNERF" false)
    (lib.cmakeBool "BUILD_NCSRUN" false)
    (lib.cmakeBool "ENABLE_MOVIE" false)
  ];

  ninjaFlags = [ "create_shaderpack" ];

  installPhase = ''
    runHook preInstall
    install -Dm644 bin/shaderpack.erf $out/share/reone/shaderpack.erf
    runHook postInstall
  '';

  meta = {
    description = "reone's packed shaders (shaderpack.erf)";
    homepage = "https://github.com/modawan/reone";
    license = lib.licenses.gpl3Plus;
    platforms = lib.platforms.linux;
  };
}

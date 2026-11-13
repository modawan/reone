# reone's engine, CLI tools and tests binary for arm64 macOS.
#
# No launcher or toolkit (wxWidgets is not cross-built here); movies play
# (FFmpeg is, with the Bink codecs). Explicit targets only: the default
# target runs the freshly built shaderpack tool, a Mach-O binary, to pack the
# shaders; shaderpack.erf comes from the native reone-shaderpack build
# instead. The tests binary is built so a Mac can run it, but nothing here
# can.
{
  lib,
  mkCross,
  llvm,
  deps,
  deploymentTarget,
  shaderpack,
  version,
}:

mkCross {
  pname = "reone-aarch64-darwin";
  inherit version;

  src = import ../source.nix { inherit lib; };

  crossInputs = [ deps ];

  cmakeFlags = [
    (lib.cmakeBool "BUILD_LAUNCHER" false)
    (lib.cmakeBool "BUILD_TOOLKIT" false)
    (lib.cmakeBool "ENABLE_MOVIE" true)
    (lib.cmakeBool "BUILD_TESTS" true)
  ];

  ninjaFlags = [
    "engine"
    "tests"
    "dataminer"
    "gff2json"
    "unerf"
    "ncsrun"
  ];

  installPhase = ''
    runHook preInstall
    for exe in engine tests dataminer gff2json unerf ncsrun; do
      install -Dm755 bin/$exe $out/bin/$exe
    done
    install -Dm644 ${shaderpack}/share/reone/shaderpack.erf $out/bin/shaderpack.erf
    runHook postInstall
  '';

  # What a Mac needs from the binaries: arm64 Mach-O executables, with the
  # ad-hoc code signature zig's linker writes (Apple silicon refuses to run
  # unsigned binaries; only the load command is checked, not the signature),
  # built for the deployment target, linked against the frameworks and
  # nothing from the Linux host.
  doInstallCheck = true;
  installCheckPhase = ''
    runHook preInstallCheck
    for exe in engine tests dataminer gff2json unerf ncsrun; do
      fileHeader=$(${llvm}/bin/llvm-readobj --file-header $out/bin/$exe)
      grep -q 'Format: Mach-O arm64' <<<"$fileHeader" \
        || { echo "install check: $exe is not a Mach-O arm64 executable"; exit 1; }
      headers=$(${llvm}/bin/llvm-objdump --macho --private-headers $out/bin/$exe)
      grep -q LC_CODE_SIGNATURE <<<"$headers" \
        || { echo "install check: $exe has no code signature"; exit 1; }
      grep -qE 'minos ${deploymentTarget}$' <<<"$headers" \
        || { echo "install check: $exe is not built for macOS ${deploymentTarget}"; grep minos <<<"$headers"; exit 1; }
    done
    # The first line of --dylibs-used is the binary's own path, a store path.
    dylibs=$(${llvm}/bin/llvm-objdump --macho --dylibs-used $out/bin/engine | tail -n +2)
    for fw in Cocoa OpenGL CoreAudio libSystem; do
      grep -q "$fw" <<<"$dylibs" || { echo "install check: engine does not link $fw"; echo "$dylibs"; exit 1; }
    done
    grep -q /nix/store <<<"$dylibs" && { echo "install check: engine links a store path"; echo "$dylibs"; exit 1; }
    # Movies: the Bink decoder is in the binary, so FFmpeg was linked with it.
    symbols=$(${llvm}/bin/llvm-nm --defined-only $out/bin/engine)
    grep -qE ' _ff_bink_decoder$' <<<"$symbols" \
      || { echo "install check: engine has no Bink decoder (FFmpeg not linked?)"; exit 1; }
    [ -s $out/bin/shaderpack.erf ] || { echo "install check: shaderpack.erf missing"; exit 1; }
    runHook postInstallCheck
  '';

  meta = {
    description = "reone engine, CLI tools and tests for arm64 macOS, cross-built on Linux";
    homepage = "https://github.com/modawan/reone";
    license = lib.licenses.gpl3Plus;
    # Where it is built; it runs on aarch64-darwin.
    platforms = lib.platforms.linux;
    mainProgram = "engine";
  };
}

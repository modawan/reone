# FFmpeg for arm64 macOS, static, cut down to what the game's movies need:
# the Bink demuxer and the Bink video and audio decoders (every KotOR and
# TSL movie is a .bik), file input, swscale (the engine converts frames to
# RGB24) and swresample. Same source as nixpkgs' ffmpeg. No external
# libraries, no hardware acceleration, no programs: reone links the five
# libraries through pkg-config and decodes with the generic API.
#
# FFmpeg's own configure cross-compiles with the wrappers; its compile tests
# run them, so the toolchain's flags apply there too. Its build-time helpers
# need a native C compiler (--host-cc), the one thing here that is not zig.
{
  mkCross,
  stdenv,
  ffmpeg,
  llvm,
}:

mkCross {
  pname = "ffmpeg";
  inherit (ffmpeg) version src;

  nativeBuildInputs = [
    llvm
    stdenv.cc
  ];

  configurePhase = ''
    runHook preConfigure
    ./configure \
      --prefix=$out \
      --enable-cross-compile --target-os=darwin --arch=aarch64 \
      --cc=zig-cc --cxx=zig-c++ --ar=zig-ar --ranlib=zig-ranlib --nm=llvm-nm --strip=llvm-strip \
      --host-cc=gcc \
      --enable-static --disable-shared --enable-pic \
      --disable-programs --disable-doc --disable-autodetect --disable-network \
      --disable-avdevice --disable-avfilter \
      --disable-everything \
      --enable-demuxer=bink --enable-decoder=bink,binkaudio_dct,binkaudio_rdft \
      --enable-protocol=file \
      --enable-swscale --enable-swresample
    runHook postConfigure
  '';

  buildPhase = ''
    runHook preBuild
    make -j$NIX_BUILD_CORES
    runHook postBuild
  '';

  # The install rule runs ranlib once more on the installed copy
  # (LIB_INSTALL_EXTRA_CMD); the symbol tables are already in the archives,
  # so that step is skipped.
  installPhase = ''
    runHook preInstall
    make install LIB_INSTALL_EXTRA_CMD=
    runHook postInstall
  '';

  # After the fixup phase, the last thing that touches the outputs: zig's
  # linker reads darwin-format archives only (see dontStrip in default.nix).
  postFixup = ''
    for a in $out/lib/*.a; do
      head -c 70 $a | grep -q '#1/' || { echo "$a is not a darwin-format archive"; exit 1; }
    done
  '';

  inherit (ffmpeg) meta;
}

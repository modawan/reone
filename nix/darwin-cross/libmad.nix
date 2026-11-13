# libmad for arm64 macOS, static. Same source as nixpkgs' libmad and the
# patches it applies with its GCC stdenv (CVE fixes); the extra one nixpkgs
# adds for clang touches only configure.ac, which is not run here.
#
# libmad's 2004 configure script is not run: it probes the build machine for
# ld, a C++ compiler and a C++ preprocessor (libtool), none of which the cross
# build has, and its generated Makefile adds GCC-only optimisation flags that
# clang rejects. Its config.h for aarch64-apple-darwin is recorded in
# libmad-config.h; the objects are compiled and archived directly, and mad.h
# is generated the way the Makefile generates it (the tarball's mad.h is for
# the configuration it was released from: FPM_INTEL, 32-bit long). reone
# finds the result through its own cmake/FindMAD.cmake (mad.h and libmad.a),
# so no .pc file is needed.
{
  mkCross,
  libmad,
}:

mkCross {
  pname = "libmad";
  inherit (libmad) version src patches;

  dontConfigure = true;

  # Makefile.am's mad.h rule: the copyright as a comment, the FPM define, the
  # HAVE_*_ASM, OPT_* and SIZEOF_* defines from config.h, then the exported
  # headers through mad.h.sed, all inside extern "C".
  buildPhase = ''
    runHook preBuild
    cp ${./libmad-config.h} config.h
    for c in version bit timer stream frame synth decoder layer12 layer3 huffman fixed; do
      zig-cc -O2 -DHAVE_CONFIG_H -DFPM_64BIT -I. -c $c.c -o $c.o
    done
    zig-ar rcs libmad.a *.o

    (
      sed -e '1s|.*|/*|' -e '1b' -e '$s|.*| */|' -e '$b' -e 's/^.*/ *&/' COPYRIGHT
      echo
      echo "# ifdef __cplusplus"
      echo 'extern "C" {'
      echo "# endif"
      echo
      echo "# define FPM_64BIT"
      echo
      sed -ne 's/^# *define  *\(HAVE_.*_ASM\).*/# define \1/p' config.h
      echo
      sed -ne 's/^# *define  *OPT_\(SPEED\|ACCURACY\).*/# define OPT_\1/p' config.h
      echo
      sed -ne 's/^# *define  *\(SIZEOF_.*\)/# define \1/p' config.h
      echo
      for header in version.h fixed.h bit.h timer.h stream.h frame.h synth.h decoder.h; do
        echo
        sed -n -f mad.h.sed $header
      done
      echo
      echo "# ifdef __cplusplus"
      echo '}'
      echo "# endif"
    ) > mad.h
    grep -q '^# define FPM_64BIT$' mad.h
    grep -q '^# define SIZEOF_LONG 8$' mad.h
    runHook postBuild
  '';

  installPhase = ''
    runHook preInstall
    install -Dm644 libmad.a $out/lib/libmad.a
    install -Dm644 mad.h $out/include/mad.h
    runHook postInstall
  '';

  inherit (libmad) meta;
}

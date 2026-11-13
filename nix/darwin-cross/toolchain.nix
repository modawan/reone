# A CMake toolchain that builds for arm64 macOS on Linux: zig's bundled clang
# and its own Mach-O linker for compiling and linking, the macOS SDK for
# framework headers and link stubs, and llvm's install_name_tool/otool/lipo
# stand-ins, which CMake's Darwin platform insists on finding.
#
# Output layout:
#   bin/zig-cc, bin/zig-c++, bin/zig-ar, bin/zig-ranlib  compiler wrappers
#   share/cmake/zig-darwin-arm64.cmake                   the toolchain file
#
# What the wrappers do, each line of it paid for with a failed build:
# - No --sysroot: zig prefixes it onto every -L directory, so the cross-built
#   libraries would be looked for under the SDK. zig never adds the SDK's
#   usr/include anyway (it ships its own copy of the macOS libc headers, from
#   SDK 27 with zig 0.17, which therefore shadow the SDK's) and does not
#   derive the framework directory from CMake's -isysroot either.
# - Compiling: -iframework rather than -F makes the frameworks system headers,
#   as Apple's clang does for its sysroot, so the SDK's own diagnostics
#   (-Wundef, elaborated enum bases in CoreFoundation) stay quiet under a
#   project's -Werror. The SDK's C headers go last (-idirafter, also a system
#   directory) so libc++'s wrapper headers keep precedence; first, every C++
#   compile fails with "<cstddef> tried including <stddef.h>".
# - Linking: -F and -L for the SDK's framework and library stubs (libobjc,
#   libm, ...); -framework X and -weak_framework X inside -Wl, tokens, which
#   static .pc files and OpenAL Soft's CMake config emit (the latter several
#   per token: -Wl,-framework,AudioUnit,-framework,ApplicationServices),
#   become plain -framework X, which zig applies and -Wl, does not; -latomic
#   goes (macOS has no libatomic, the builtins cover it), and so does
#   -Wl,-dynamic (ld64's default, which FFmpeg's configure passes on darwin
#   and zig rejects). The -L<sdk>/usr/lib was also
#   load-bearing with zig 0.16: without it that Mach-O linker segfaulted on
#   the engine link as soon as Foundation's stub was an input (a small Cocoa
#   program linked either way); not re-tested without it on 0.17. zig 0.15
#   does not crash but resolves no framework symbol from these stubs at all.
# - The deployment target has to be in zig's target triple: zig ignores
#   -mmacosx-version-min (CMake's CMAKE_OSX_DEPLOYMENT_TARGET) and would stamp
#   its own default into the binaries. The SDK version it stamps next to it
#   (LC_BUILD_VERSION's sdk field) is that of its bundled headers, 27.0 with
#   zig 0.17, not of the SDK linked against.
# Limits: a link is recognised by the absence of -c, -E, -S, -M, -MM and
# -fsyntax-only on the command line, so arguments inside @response files are
# neither seen nor rewritten (Ninja writes one for a link whose command line
# exceeds the platform limit), a framework split over two tokens
# (-Wl,-framework -Wl,X) is not recognised, and a comma inside a -Wl, value
# is taken as a separator. reone's build hits none of these today.
#
# Two LLVMs are in play: zig bundles its own (22 with zig 0.17) for
# compiling and linking; the install_name_tool, otool and lipo stand-ins are
# nixpkgs' llvm, which only reads the Mach-O files.
{
  lib,
  runCommand,
  runtimeShell,
  zig,
  llvm,
  sdk,
  # Oldest macOS the binaries run on.
  deploymentTarget ? "12.0",
}:

let
  # @tool@ is cc or c++, substituted when the two wrappers are installed.
  wrapper = ''
    #!${runtimeShell}
    args=()
    compiling=0
    for a in "$@"; do
      case "$a" in
        -c|-E|-S|-M|-MM|-fsyntax-only) compiling=1 ;;
      esac
    done
    for a in "$@"; do
      case "$a" in
        -Wl,*)
          # One -Wl, token may carry several linker arguments: pass
          # -framework X and -weak_framework X to zig as such (it ignores them
          # behind -Wl,), hand the rest on one per -Wl, token.
          IFS=, read -r -a pieces <<<"''${a#-Wl,}"
          i=0
          while [ "$i" -lt "''${#pieces[@]}" ]; do
            piece=''${pieces[$i]}
            if { [ "$piece" = -framework ] || [ "$piece" = -weak_framework ]; } \
                && [ "$((i + 1))" -lt "''${#pieces[@]}" ]; then
              args+=("$piece" "''${pieces[$((i + 1))]}")
              i=$((i + 2))
            elif [ "$piece" = -dynamic ]; then
              # ld64's default, which FFmpeg's configure spells out for darwin
              # and zig refuses.
              i=$((i + 1))
            else
              args+=("-Wl,$piece")
              i=$((i + 1))
            fi
          done
          ;;
        -latomic) ;;
        *) args+=("$a") ;;
      esac
    done
    sdkflags=(-iframework ${sdk}/System/Library/Frameworks -idirafter ${sdk}/usr/include)
    if [ "$compiling" = 0 ]; then
      sdkflags+=(-F${sdk}/System/Library/Frameworks -L${sdk}/usr/lib)
    fi
    exec ${zig}/bin/zig @tool@ -target aarch64-macos.${deploymentTarget} "''${sdkflags[@]}" "''${args[@]}"
  '';

  toolchainFile = ''
    set(CMAKE_SYSTEM_NAME Darwin)
    set(CMAKE_SYSTEM_PROCESSOR arm64)
    # The Darwin kernel version of macOS 12, the default deployment target.
    # Informational: nothing in reone's build reads it.
    set(CMAKE_SYSTEM_VERSION 21)

    set(CMAKE_OSX_SYSROOT "${sdk}" CACHE PATH "macOS SDK")
    set(CMAKE_OSX_ARCHITECTURES arm64 CACHE STRING "")
    set(CMAKE_OSX_DEPLOYMENT_TARGET "${deploymentTarget}" CACHE STRING "")

    set(CMAKE_C_COMPILER "@out@/bin/zig-cc")
    set(CMAKE_CXX_COMPILER "@out@/bin/zig-c++")
    set(CMAKE_OBJC_COMPILER "@out@/bin/zig-cc")
    set(CMAKE_OBJCXX_COMPILER "@out@/bin/zig-c++")
    set(CMAKE_AR "@out@/bin/zig-ar" CACHE FILEPATH "")
    set(CMAKE_RANLIB "@out@/bin/zig-ranlib" CACHE FILEPATH "")
    set(CMAKE_C_COMPILER_AR "''${CMAKE_AR}" CACHE FILEPATH "")
    set(CMAKE_CXX_COMPILER_AR "''${CMAKE_AR}" CACHE FILEPATH "")
    set(CMAKE_C_COMPILER_RANLIB "''${CMAKE_RANLIB}" CACHE FILEPATH "")
    set(CMAKE_CXX_COMPILER_RANLIB "''${CMAKE_RANLIB}" CACHE FILEPATH "")

    # CMake's Darwin platform refuses to configure without install_name_tool.
    set(CMAKE_INSTALL_NAME_TOOL "${llvm}/bin/llvm-install-name-tool" CACHE FILEPATH "")
    set(CMAKE_OTOOL "${llvm}/bin/llvm-otool" CACHE FILEPATH "")
    set(CMAKE_LIPO "${llvm}/bin/llvm-lipo" CACHE FILEPATH "")

    # Headers, libraries and packages come from the SDK and from the prefixes
    # given as CMAKE_PREFIX_PATH (the cross-built dependencies), never from the
    # Linux host. Programs (pkg-config, ...) are host tools. The dev shell
    # exports CMAKE_PREFIX_PATH as an environment variable, which CMake reads
    # in its find commands but never copies into the variable, so append it:
    # a -DCMAKE_PREFIX_PATH on the command line adds to it, not replaces it.
    if(DEFINED ENV{CMAKE_PREFIX_PATH})
      string(REPLACE ":" ";" _env_prefix_path "$ENV{CMAKE_PREFIX_PATH}")
      list(APPEND CMAKE_PREFIX_PATH ''${_env_prefix_path})
    endif()
    set(CMAKE_FIND_ROOT_PATH "${sdk}" ''${CMAKE_PREFIX_PATH})
    set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
    set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
    set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
    set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
    set(CMAKE_FIND_FRAMEWORK FIRST)

    # pkg-config answers only from those prefixes, and with Libs.private
    # (--static): the dependencies are static libraries, so their frameworks
    # have to reach the final link line.
    set(_pkgconfig_dirs "")
    foreach(_prefix IN LISTS CMAKE_PREFIX_PATH)
      list(APPEND _pkgconfig_dirs "''${_prefix}/lib/pkgconfig")
    endforeach()
    list(JOIN _pkgconfig_dirs ":" _pkgconfig_dirs)
    set(ENV{PKG_CONFIG_LIBDIR} "''${_pkgconfig_dirs}")
    set(ENV{PKG_CONFIG_PATH} "")
    set(ENV{PKG_CONFIG_SYSROOT_DIR} "")
    set(PKG_CONFIG_ARGN "--static" CACHE STRING "")

    # Every prefix's lib directory on the link path: a library that a project
    # links by name only (pkg-config reports a library's name and its -L
    # separately, and not every CMake file passes both on) is found there, as
    # it would be in a native build's default search path.
    set(_link_dirs "")
    foreach(_prefix IN LISTS CMAKE_PREFIX_PATH)
      string(APPEND _link_dirs " -L''${_prefix}/lib")
    endforeach()
    set(CMAKE_EXE_LINKER_FLAGS_INIT "''${_link_dirs}")
  '';
in
runCommand "zig-darwin-arm64-toolchain"
  {
    inherit wrapper toolchainFile;
    passAsFile = [
      "wrapper"
      "toolchainFile"
    ];
    passthru = {
      inherit
        sdk
        zig
        llvm
        deploymentTarget
        ;
      cmakeToolchainFile = "share/cmake/zig-darwin-arm64.cmake";
    };
    meta.description = "CMake toolchain for building arm64 macOS binaries on Linux with zig and the macOS SDK";
  }
  ''
    mkdir -p $out/bin $out/share/cmake
    sed 's|@tool@|cc|' $wrapperPath > $out/bin/zig-cc
    sed 's|@tool@|c++|' $wrapperPath > $out/bin/zig-c++
    # ar and ranlib take no target or SDK flags. Archives in the platform's
    # format: llvm-ar picks it on its own when CMake drives it, but FFmpeg's
    # build got GNU-format archives with a BSD symbol table from the ranlib
    # step, which zig's linker reads as an object ("unknown cpu architecture").
    printf '#!${runtimeShell}\nexec ${zig}/bin/zig ar --format=darwin "$@"\n' > $out/bin/zig-ar
    printf '#!${runtimeShell}\nexec ${zig}/bin/zig ranlib "$@"\n' > $out/bin/zig-ranlib
    chmod +x $out/bin/zig-*
    sed "s|@out@|$out|g" $toolchainFilePath > $out/share/cmake/zig-darwin-arm64.cmake
  ''

# OpenAL Soft for arm64 macOS, static, CoreAudio backend. Same source as
# nixpkgs' openal-soft. reone links OpenAL Soft on every platform (Apple's
# own OpenAL framework is deprecated and has no EFX), though on macOS
# src/pch.h still compiles against the framework's headers, as upstream's
# macOS build does.
{
  mkCross,
  openal-soft,
}:

mkCross {
  pname = "openal-soft";
  inherit (openal-soft) version src;

  # The bundled fmt 11.1.x calls malloc and free without including <cstdlib>.
  # Apple's libc++ includes it transitively; zig's bundled libc++ (LLVM 22
  # with zig 0.17) does not. --replace-fail so that an OpenAL Soft bump that
  # fixes or drops it shows up here instead of silently patching nothing.
  postPatch = ''
    substituteInPlace fmt-*/include/fmt/format.h \
      --replace-fail '#  include <cstring>  // std::memcpy' \
                     '#  include <cstdlib>  // std::malloc, std::free
    #  include <cstring>  // std::memcpy'
  '';

  cmakeFlags = [
    "-DLIBTYPE=STATIC"
    "-DALSOFT_UTILS=OFF"
    "-DALSOFT_EXAMPLES=OFF"
    "-DALSOFT_INSTALL_EXAMPLES=OFF"
    "-DALSOFT_INSTALL_UTILS=OFF"
    "-DALSOFT_BACKEND_COREAUDIO=ON"
    "-DALSOFT_REQUIRE_COREAUDIO=ON"
  ];

  inherit (openal-soft) meta;
}

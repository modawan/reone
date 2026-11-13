# SDL3 for arm64 macOS, static. Same source as nixpkgs' sdl3.
{
  mkCross,
  sdl3,
}:

mkCross {
  pname = "sdl3";
  inherit (sdl3) version src;

  cmakeFlags = [
    "-DSDL_SHARED=OFF"
    "-DSDL_STATIC=ON"
    "-DSDL_TESTS=OFF"
    "-DSDL_EXAMPLES=OFF"
    "-DSDL_TEST_LIBRARY=OFF"
    "-DSDL_INSTALL_DOCS=OFF"
  ];

  inherit (sdl3) meta;
}

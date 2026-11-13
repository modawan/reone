# GoogleTest (gtest and gmock) for arm64 macOS, static, so the tests binary
# links. Same source as nixpkgs' gtest. The binary cannot run on the build
# machine; it is for running on a Mac.
{
  mkCross,
  gtest,
}:

mkCross {
  pname = "gtest";
  inherit (gtest) version src;

  cmakeFlags = [
    "-DBUILD_SHARED_LIBS=OFF"
    "-DBUILD_GMOCK=ON"
  ];

  inherit (gtest) meta;
}

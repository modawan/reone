# The Boost libraries reone uses, for arm64 macOS: the compiled ones static,
# with the BoostConfig.cmake files find_package needs, and the headers of the
# header-only ones.
#
# Built with Boost's own CMake support, which only the "-cmake" release
# archive carries (the classic boost_1_XX_0 tarball nixpkgs uses has no root
# CMakeLists.txt). Boost.Build would need its darwin toolset taught about zig.
{
  lib,
  mkCross,
  fetchurl,
}:

let
  version = "1.89.0";
in
mkCross {
  pname = "boost";
  inherit version;

  src = fetchurl {
    url = "https://github.com/boostorg/boost/releases/download/boost-${version}/boost-${version}-cmake.tar.xz";
    hash = "sha256-Z6zsAtDRGLXenrRB9ftwezoc3YhL4AyiS5pzyZVRH3Q=";
  };

  cmakeFlags = [
    "-DBUILD_SHARED_LIBS=OFF"
    # The compiled libraries reone links, then the header-only ones it
    # includes (grep '#include <boost/' over include, src and test): Boost's
    # CMake installs only the listed libraries' headers and their
    # dependencies'.
    "-DBOOST_INCLUDE_LIBRARIES=program_options;json;exception;regex;algorithm;container;format;functional;endian;core"
    "-DBOOST_INSTALL_LAYOUT=system"
    "-DBUILD_TESTING=OFF"
  ];

  meta = {
    description = "Boost C++ libraries, the subset reone links, for arm64 macOS";
    homepage = "https://www.boost.org";
    license = lib.licenses.boost;
    platforms = lib.platforms.linux;
  };
}

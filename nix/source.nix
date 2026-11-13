# Only what CMake reads. Keeps nix/, flake files and docs edits from
# invalidating the build. Shared by the native package and the macOS cross
# build.
{ lib }:

lib.fileset.toSource {
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
    ../.clang-format
  ];
}

# The macOS SDK as nixpkgs fetches it for its own Darwin builds: Apple's
# Command Line Tools package from Apple's CDN, unpacked to the SDK directory
# (frameworks, headers and .tbd link stubs; no binaries). Reusing nixpkgs'
# fetcher and hashes means the result is the same store path Hydra builds, so
# cache.nixos.org serves it to Linux machines; on a cache miss the fetcher
# downloads it from swcdn.apple.com like any other fixed-output derivation.
#
# Only the download is reused. nixpkgs' apple-sdk derivation itself is
# darwin-only (it propagates darwin-only packages), so it is not used here.
# The fetcher and its version table are nixpkgs internals, not an interface:
# a nixpkgs refactor of pkgs/by-name/ap/apple-sdk breaks this evaluation, and
# the fast CI check (nix flake check --no-build) will say so.
{
  lib,
  callPackage,
  path,
  # The SDK major version. nixpkgs' metadata lists the versions it knows.
  darwinSdkMajorVersion ? "26",
}:

let
  sdkDir = path + "/pkgs/by-name/ap/apple-sdk";
  versions = builtins.fromJSON (builtins.readFile (sdkDir + "/metadata/versions.json"));
  fetchSDK = callPackage (sdkDir + "/common/fetch-sdk.nix") { };
  sdkInfo =
    versions.${darwinSdkMajorVersion}
      or (throw "nixpkgs has no macOS SDK ${darwinSdkMajorVersion}; known: ${lib.concatStringsSep ", " (builtins.attrNames versions)}");
in
fetchSDK sdkInfo

{
  description = "reone: an open source engine for Star Wars: KotOR and TSL";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs =
    { self, nixpkgs }:
    let
      # Only x86_64-linux is built and tested (locally and in CI). Add other
      # systems here once someone has built them.
      systems = [ "x86_64-linux" ];
      forAll = f: nixpkgs.lib.genAttrs systems (system: f system nixpkgs.legacyPackages.${system});

      # nixpkgs convention for unreleased builds: 0-unstable-YYYY-MM-DD,
      # dated by the last commit.
      date = self.lastModifiedDate or "19700101000000";
      version = "0-unstable-${builtins.substring 0 4 date}-${builtins.substring 4 2 date}-${builtins.substring 6 2 date}";

      app = system: name: description: {
        type = "app";
        program = "${self.packages.${system}.reone}/bin/${name}";
        meta = { inherit description; };
      };

      # arm64 macOS binaries built on Linux (zig + the macOS SDK), see
      # nix/darwin-cross/default.nix. The package needs the native
      # reone-shaderpack for shaderpack.erf; the shell does not. Built on
      # Linux only: its meta.platforms is linux, so a darwin entry in
      # `systems` would need it excluded there.
      darwinCross = forAll (
        system: pkgs:
        import ./nix/darwin-cross {
          inherit pkgs version;
          shaderpack = self.packages.${system}.reone-shaderpack;
        }
      );
    in
    {
      packages = forAll (
        system: pkgs: rec {
          reone = pkgs.callPackage ./nix/package.nix { inherit version; };
          default = reone;
          # shaderpack.erf alone, natively: what the cross build and its dev
          # shell need from a native build.
          reone-shaderpack = pkgs.callPackage ./nix/shaderpack.nix { inherit version; };
          reone-aarch64-darwin = darwinCross.${system}.reone;
        }
      );

      apps = forAll (
        system: pkgs: {
          default = app system "reone" "reone engine";
          launcher = app system "reone-launcher" "reone launcher";
          toolkit = app system "reone-toolkit" "reone toolkit";
        }
      );

      checks = forAll (
        system: pkgs: {
          # Builds everything, runs the unit tests and the install check.
          reone = self.packages.${system}.reone;
          # The run wrapper's behaviour, against a stub store. Seconds, no
          # C++ build.
          reone-run =
            pkgs.runCommand "reone-run-test"
              {
                nativeBuildInputs = [ pkgs.bash ];
              }
              ''
                bash ${./nix/reone-run-test.sh} ${./nix/reone-run.sh}
                touch $out
              '';
          # reone-aarch64-darwin is not a check: its own workflow
          # (build-macos-cross.yml) builds it, with its install check, on every
          # pull request, and the full check that runs for release tags would
          # only build it a second time. Keeping it out of the check also
          # keeps the SDK licence question (nix/darwin-cross/default.nix) in
          # one place, that workflow.
        }
      );

      devShells = forAll (
        system: pkgs: {
          # Same toolchain and libraries as the package, for iterating with
          # cmake/ninja by hand:
          #   cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
          #   cmake --build build && ctest --test-dir build --output-on-failure
          default = pkgs.mkShell {
            inputsFrom = [ self.packages.${system}.reone ];
            packages = with pkgs; [
              clang-tools
              gdb
            ];
            # Lets clangd find build/compile_commands.json.
            CMAKE_EXPORT_COMPILE_COMMANDS = "ON";
          };
          # The macOS cross toolchain and libraries; it prints the cmake and
          # ninja lines to run.
          aarch64-darwin = darwinCross.${system}.shell;
        }
      );

      formatter = forAll (system: pkgs: pkgs.nixfmt);
    };
}

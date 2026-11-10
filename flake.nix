{
  description = "reone: an open source engine for Star Wars: KotOR and TSL";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs =
    { self, nixpkgs }:
    let
      # x86_64-linux is built and tested locally and in CI. aarch64-darwin is
      # built in CI only: the macOS job in .github/workflows/build-test-nix.yml
      # runs the full build whenever that workflow runs (flake, nix/ or
      # workflow changes, and release tags). Intel Macs are not here: nixpkgs
      # unstable dropped x86_64-darwin in 26.11, so building for them would
      # need a second input pinned to a 26.05 branch.
      systems = [
        "x86_64-linux"
        "aarch64-darwin"
      ];
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
    in
    {
      packages = forAll (
        system: pkgs: rec {
          reone = pkgs.callPackage ./nix/package.nix { inherit version; };
          default = reone;
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
        }
      );

      # Same toolchain and libraries as the package, for iterating with
      # cmake/ninja by hand:
      #   cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
      #   cmake --build build && ctest --test-dir build --output-on-failure
      devShells = forAll (
        system: pkgs: {
          default = pkgs.mkShell {
            inputsFrom = [ self.packages.${system}.reone ];
            packages = [
              pkgs.clang-tools
            ]
            # nixpkgs builds gdb on aarch64-darwin for an x86_64 target, so
            # it cannot debug an arm64 process; lldb can.
            ++ pkgs.lib.optionals pkgs.stdenv.hostPlatform.isLinux [ pkgs.gdb ]
            ++ pkgs.lib.optionals pkgs.stdenv.hostPlatform.isDarwin [ pkgs.lldb ];
            # Lets clangd find build/compile_commands.json.
            CMAKE_EXPORT_COMPILE_COMMANDS = "ON";
          };
        }
      );

      formatter = forAll (system: pkgs: pkgs.nixfmt);
    };
}

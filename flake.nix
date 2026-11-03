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
            packages = with pkgs; [
              clang-tools
              gdb
            ];
            # Lets clangd find build/compile_commands.json.
            CMAKE_EXPORT_COMPILE_COMMANDS = "ON";
          };
        }
      );

      formatter = forAll (system: pkgs: pkgs.nixfmt);
    };
}

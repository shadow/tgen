# This is a nix flake. See https://nixos.wiki/wiki/Flakes
#
# nix support is currently experimental.
{
  description = "tgen: traffic generator";

  inputs = rec {
    # Specify the (currently) latest release tag.
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-21.11";

    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = nixpkgs.legacyPackages.${system};
        version = "1.0.0";
      in rec {
        packages = flake-utils.lib.flattenTree rec {
          tgen = pkgs.stdenv.mkDerivation {
            pname = "tgen";
            version = "${version}";
            src = self;

            buildInputs = with pkgs; [
              cmake
              glib
              igraph
              pkg-config
            ];
          };
          # TODO: use pkgs.poetry2nix to avoid having to duplicate python
          # package requirements here.  For some reason adding a pyproject.toml
          # (required by poetry) breaks the non-nix workflow.
          tgentools = pkgs.python3.pkgs.buildPythonApplication {
            pname = "tgentools";
            version = "${version}";
            src = "${self}/tools";
            propagatedBuildInputs = [
              (pkgs.python3.withPackages (pythonPackages: with pythonPackages; [
                matplotlib
                networkx
                numpy
                scipy
              ]))
            ];
          };
        };
        defaultPackage = packages.tgen;

        apps.tgen = flake-utils.lib.mkApp { drv = packages.tgen; };
        apps.tgentools = flake-utils.lib.mkApp { drv = packages.tgentools; };
        defaultApp = apps.tgen;

        checks.all = pkgs.stdenv.mkDerivation rec {
          name = "tgen-test";
          src = self;

          tgen = packages.tgen;
          tgentools = packages.tgentools;

          # We need tgen's build inputs again to build the test binary, in
          # addition to tgen and tgentools themselves. We also need the tgen
          # build inputs again for the configure step, which runs cmake.
          #
          # XXX: Ideally our CMakeLists would be more modular s.t. we didn't
          # need tgen's build requirements here, since we're not building it
          # again.
          buildInputs = packages.tgen.buildInputs ++ [
            tgen
            tgentools
            pkgs.bash
            pkgs.gnugrep
            pkgs.diffutils
            pkgs.coreutils
          ];
          buildPhase = ''
            echo 'Building test binary'
            (cd test && make)

            # Go back to root of build dir
            cd ..

            echo 'Running mmodel tests'
            bash test/run_mmodel_tests.sh

            echo 'Running tgen integration tests'
            bash test/run_tgen_integration_tests.sh --tgen $tgen/bin/tgen

            # Ideally this would be a separate check for the tgentools
            # package. Currently this script relies on output from
            # test/run_tgen_integration_tests.sh though, so it'll take some
            # work to separate them.
            echo 'Running tgentools integration tests'
            bash test/run_tgentools_integration_tests.sh --tgentools $tgentools/bin/tgentools
          '';
          # Need to create output directory to satisfy nix.
          installPhase = "mkdir -p $out";
        };
      }
    );
}

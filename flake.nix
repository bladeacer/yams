{
  description = "Yams: High-performance media visualizer TUI";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs =
    {
      self,
      nixpkgs,
      flake-utils,
    }:
    flake-utils.lib.eachDefaultSystem (
      system:
      let
        pkgs = nixpkgs.legacyPackages.${system};
        optimFlags =
          if pkgs.stdenv.isx86_64 then
            ",-march=native,-mtune=native"
          else if pkgs.stdenv.isAarch64 && pkgs.stdenv.isLinux then
            ",-march=native"
          else if pkgs.stdenv.isDarwin then
            ",-mcpu=apple-m1"
          else
            "";

      in
      {
        packages.default = pkgs.stdenv.mkDerivation {
          pname = "yams";
          version = "1.0.0";
          src = ./.;

          nativeBuildInputs = with pkgs; [
            meson
            ninja
            pkg-config
          ];
          buildInputs =
            with pkgs;
            [
              ncurses
              ffmpeg
              chafa
              glib
            ]
            ++ lib.optionals (!pkgs.stdenv.isDarwin) [
              playerctl
            ];

          mesonFlags = [
            "--buildtype=release"
            "--optimization=3"
            "-Db_lto=true"
            "-Dstrip=true"
            "-Dtests=false"
            "-Dlogging=false"
            "-Dc_args=-DNDEBUG,-O3,-fno-plt${optimFlags}"
          ];
        };

        devShells.default = pkgs.mkShell {
          packages =
            with pkgs;
            [
              gnumake
              meson
              ninja
              pkg-config
              cmake
              ncurses
              playerctl
              ffmpeg
              chafa
              glib
              cmocka
              hyperfine
            ]
            ++ pkgs.lib.optionals pkgs.stdenv.isLinux [
              valgrind
              procps
              util-linux
            ];
        };
      }
    );
}

{
  description = "Yams: High-performance media visualizer TUI";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
    mediaremote-adapter = {
      url = "github:ungive/mediaremote-adapter";
      flake = false;
    };
  };

  outputs =
    {
      self,
      nixpkgs,
      flake-utils,
      mediaremote-adapter,
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

        adapterPkg =
          if pkgs.stdenv.isDarwin then
            pkgs.stdenv.mkDerivation {
              pname = "mediaremote-adapter";
              version = "unstable";
              src = mediaremote-adapter;

              nativeBuildInputs = [
                pkgs.cmake
                (pkgs.writeShellScriptBin "codesign" ''exec /usr/bin/codesign "$@"'')
              ];

              configurePhase = "cmake . -DCMAKE_BUILD_TYPE=Release";
              buildPhase = "cmake --build .";
              installPhase = ''
                mkdir -p $out/bin
                cp -r MediaRemoteAdapter.framework $out/bin/
                cp $src/bin/mediaremote-adapter.pl $out/bin/
              '';
            }
          else
            null;

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

          postInstall = pkgs.lib.optionalString pkgs.stdenv.isDarwin ''
            cp -r ${adapterPkg}/bin/MediaRemoteAdapter.framework $out/bin/
            cp ${adapterPkg}/bin/mediaremote-adapter.pl $out/bin/
          '';
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

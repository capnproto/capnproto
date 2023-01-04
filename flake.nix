{
  inputs = {
    nixpkgs.url = "github:nixos/nixpkgs/nixos-21.11";
    nix-filter.url = "github:numtide/nix-filter";
    flake-utils.url = "github:numtide/flake-utils";
  };
  outputs =
    { self, nixpkgs, nix-filter, flake-utils }:
    let
      supportedSystems = [ "x86_64-linux" "aarch64-linux" ];
      forAllSystems = nixpkgs.lib.genAttrs supportedSystems;
      nixpkgsFor = forAllSystems (system: import nixpkgs { inherit system; });

    in
    {
      packages = forAllSystems
        (system:
          let
            pkgs = nixpkgsFor.${system};
            selfpkgs = self.packages.${system};
          in
          {
            capnproto = pkgs.stdenv.mkDerivation {
              pname = "capnproto";
              version = "0.11";
              dontStrip = true;
              src = ./.;
              enableParallelBuilding = true;

              nativeBuildInputs = [ pkgs.cmake ];
              buildInputs = [
                pkgs.zlib
                pkgs.openssl
              ];

              cmakeFlags = [ ];

              outputs = [ "out" ];
            };
          }
        );

      defaultPackage = forAllSystems (system: self.packages.${system}.capnproto);
      devShell = forAllSystems (system: self.packages.${system}.capnproto);
    };
}

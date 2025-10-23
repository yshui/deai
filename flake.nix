{
  inputs = {
    nixpkgs.url = "github:nixos/nixpkgs";
  };
  outputs =
    { self, nixpkgs, ... }:
    let
      llvmVersion = "20";
      system = "x86_64-linux";
      profilePkgs = import nixpkgs {
        inherit system;
        overlays = [
          (final: prev: {
            stdenv = prev.withCFlags "-fno-omit-frame-pointer" prev.stdenv;
          })
          (final: prev: {
            "llvmPackages_${llvmVersion}" = prev."llvmPackages_${llvmVersion}" // {
              stdenv = final.withCFlags "-fno-omit-frame-pointer" prev."llvmPackages_${llvmVersion}".stdenv;
            };
          })
        ];
      };
      mkPkg =
        pkgs: args:
        (pkgs.callPackage ./default.nix (
          {
            llvmPackages = pkgs."llvmPackages_${llvmVersion}";
          }
          // (args pkgs)
        ));
      mkDevShell =
        pkgs: args:
        (mkPkg pkgs args).overrideAttrs (o: {
          nativeBuildInputs =
            (with pkgs; [
              clippy
              pkgs."llvmPackages_${llvmVersion}".clang-tools
              gcovr
            ])
            ++ o.nativeBuildInputs;
          hardeningDisable = [ "all" ];
        });
      pkgs' = nixpkgs.legacyPackages.${system};
    in
    rec {
      packages.${system} = rec {
        deai = mkPkg pkgs' (_: { });
        default = deai;
      };
      devShells.${system} = rec {
        default = mkDevShell pkgs' (_: { });
        clangEnv = mkDevShell pkgs' (pkgs: {
          stdenv = pkgs."llvmPackages_${llvmVersion}".stdenv;
        });
        clangProfileEnv = mkDevShell profilePkgs (pkgs: {
          stdenv = pkgs."llvmPackages_${llvmVersion}".stdenv;
        });
      };
      overlays.default = final: prev: {
        deai = mkPkg final (_: { });
      };
    };
}

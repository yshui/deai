{
  inputs = {
    nixpkgs.url = github:nixos/nixpkgs;
  };
  outputs = { self, nixpkgs, ...}:
  let
    system = "x86_64-linux";
    profilePkgs = import nixpkgs {
      inherit system;
      overlays = [
        (final: prev: {
          stdenv = prev.withCFlags "-fno-omit-frame-pointer" prev.stdenv;
        })
        (final: prev: {
          llvmPackages_18 = prev.llvmPackages_18 // {
            stdenv = final.withCFlags "-fno-omit-frame-pointer" prev.llvmPackages_18.stdenv;
          };
        })
      ];
    };
    mkDevShell = pkgs: args: (pkgs.callPackage ./default.nix (args pkgs)).overrideAttrs (o: {
      nativeBuildInputs = (with pkgs; [
        clippy clang-tools_18
      ]) ++ o.nativeBuildInputs;
      hardeningDisable = [ "all" ];
    });
    pkgs = nixpkgs.legacyPackages.${system};
  in rec {
    packages.${system}.deai = pkgs.callPackage ./default.nix {};
    devShells.${system} = rec {
      default = mkDevShell pkgs (_: {});
      clangEnv = mkDevShell pkgs (pkgs: {
        stdenv = pkgs.llvmPackages_18.stdenv;
      });
      clangProfileEnv = mkDevShell profilePkgs (pkgs: {
        stdenv = pkgs.llvmPackages_18.stdenv;
      });
    };
    overlays.default = final: prev: {
      deai = final.callPackage ./default.nix {};
    };
  };
}

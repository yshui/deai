{
  outputs = { self, nixpkgs, ...}:
  let
    system = "x86_64-linux";
    pkgs = nixpkgs.legacyPackages.${system};
    deai = pkgs.callPackage ./default.nix {};

  in rec {
    packages.${system}.deai = deai;
    devShells.${system} = rec {
      default = deai.overrideAttrs (o: {
        nativeBuildInputs = (with pkgs; [
          clippy clang-tools_17 xdotool
        ]) ++ o.nativeBuildInputs;
        hardeningDisable = [ "all" ];
      });
      clangEnv = default.override {
        stdenv = pkgs.llvmPackages_latest.stdenv;
      };
    };
    overlays.default = final: prev: {
      deai = final.callPackage ./default.nix {};
    };
  };
}

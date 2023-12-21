{
  outputs = { self, nixpkgs, ...}:
  let 
    system = "x86_64-linux";
    pkgs = nixpkgs.legacyPackages.${system};
  in rec {
    packages.${system}.deai = pkgs.callPackage ./default.nix {};
    overlays.default = final: prev: {
      deai = final.callPackage ./default.nix {};
    };
  };
}

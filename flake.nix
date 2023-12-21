{
  outputs = { self, nixpkgs, ...}:
  let 
    system = "x86_64-linux";
    pkgs = nixpkgs.legacyPackages.${system};
  in {
    packages."${system}".deai = pkgs.callPackage ./default.nix {};
  };
}

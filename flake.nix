{
  outputs = { self, nixpkgs, ...}:
  let 
    system = "x86_64-linux";
    pkgs = nixpkgs.legacyPackages.${system};
    deai = pkgs.callPackage ./default.nix {};

  in rec {
    packages.${system}.deai = deai;
    devShell.${system} = deai.overrideAttrs (o: {
      nativeBuildInputs = o.nativeBuildInputs ++ [ pkgs.clippy ];
    });
    overlays.default = final: prev: {
      deai = final.callPackage ./default.nix {};
    };
  };
}

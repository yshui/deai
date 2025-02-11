{
  inputs = { nixpkgs.url = "github:nixos/nixpkgs"; };
  outputs = { self, nixpkgs, ... }:
    let
      system = "x86_64-linux";
      profilePkgs = import nixpkgs {
        inherit system;
        overlays = [
          (final: prev: {
            stdenv = prev.withCFlags "-fno-omit-frame-pointer" prev.stdenv;
          })
          (final: prev: {
            llvmPackages_19 = prev.llvmPackages_19 // {
              stdenv = final.withCFlags "-fno-omit-frame-pointer"
                prev.llvmPackages_19.stdenv;
            };
          })
        ];
      };
      dbgstdenv = pkgs:
        pkgs.stdenv // {
          mkDerivation = args:
            pkgs.stdenv.mkDerivation (finalAttrs:
              let
                o =
                  if builtins.isFunction args then (args finalAttrs) else args;
              in o // {
                #cmakeBuildType = "Debug";
                #dontStrip = true;
                #env = (o.env or {}) // {
                #   NIX_CFLAGS_COMPILE = toString (o.env.NIX_CFLAGS_COMPILE or "") + " -ggdb -Og";
                #};
                #postInstall = builtins.replaceStrings ["-release.cmake"] ["-debug.cmake"] (o.postInstall or "");
                postPatch = (o.postPatch or "") + (if o.pname == "clang" then
                  "(cd .. && chmod -R +w clang-tools-extra && patch -Np1 < ${
                    ./nix/0001-clangd-IncludeCleaner-Use-correct-file-ID-clang18.patch
                  })"
                else
                  "");
              });
        };
      mkPkg = pkgs: args:
        (pkgs.callPackage ./default.nix ({
          llvmPackages = pkgs.llvmPackages_19;
        } // (args pkgs)));
      mkDevShell = pkgs: args:
        (mkPkg pkgs args).overrideAttrs (o: {
          nativeBuildInputs =
            (with pkgs; [ clippy llvmPackages_19.clang-tools ])
            ++ o.nativeBuildInputs;
          hardeningDisable = [ "all" ];
        });
      pkgs' = nixpkgs.legacyPackages.${system};
    in rec {
      packages.${system} = rec {
        deai = mkPkg pkgs' (_: { });
        default = deai;
      };
      devShells.${system} = rec {
        default = mkDevShell pkgs' (_: { });
        clangEnv =
          mkDevShell pkgs' (pkgs: { stdenv = pkgs.llvmPackages_19.stdenv; });
        clangProfileEnv = mkDevShell profilePkgs
          (pkgs: { stdenv = pkgs.llvmPackages_19.stdenv; });
      };
      overlays.default = final: prev: {
        deai = mkPkg final (_: { });
      };
    };
}

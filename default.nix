{ stdenv
, elfutils
, meson
, ninja
, pkgconf
, dbus
, rustPlatform
, libdisplay-info
, libffi
, libxcb
, libXau
, libXdmcp
, libev
, libxkbcommon
, libunwind
, llvmPackages_17
, lua
, systemdLibs
, cargo
, sphinx
, rustc
, python3
, xcbutilkeysyms
, xdotool
, zlib
, zstd }:
let
  python = python3.withPackages (p: [
    p.sphinx
    p.sphinx_rtd_theme
  ]);
  cargoVendor = rustPlatform.importCargoLock {
    lockFile = ./nix/scanner.lock;
  };
in
stdenv.mkDerivation {
  src = ./.;
  name = "deai";
  nativeBuildInputs = [ meson ninja pkgconf cargo rustc python llvmPackages_17.clang xdotool ];
  buildInputs = [
    libev libffi libxcb libXau libXdmcp xcbutilkeysyms libxkbcommon dbus systemdLibs lua libdisplay-info
    libunwind elfutils zlib zstd
  ];
  env = {
    LLVM_CONFIG_PATH = "${llvmPackages_17.libllvm.dev}/bin/llvm-config";
    LIBCLANG_PATH = "${llvmPackages_17.libclang.lib}/lib";
  };
  outputs = [ "out" "doc" ];
  passthru.providedSessions = [ "deai" ];
  prePatch = ''
    mkdir docs/scanner/.cargo
    echo "[source.crates-io]
replace-with = \"vendored-sources\"
[source.vendored-sources]
directory = \"${cargoVendor}\"
[net]
offline = true" > docs/scanner/.cargo/config.toml
    cp nix/scanner.lock docs/scanner/Cargo.lock
  '';
  postBuild = ''
    mkdir -p $doc/share/doc/deai
    python -m sphinx ../docs $doc/share/doc/deai/html
  '';
}

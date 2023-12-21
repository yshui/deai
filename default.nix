{ stdenv
, meson
, ninja
, pkgconf
, dbus
, libffi
, libxcb
, libXau
, libXdmcp
, libev
, libxkbcommon
, lua
, systemdLibs
, xcbutilkeysyms }:

stdenv.mkDerivation {
  src = ./.;
  name = "deai";
  nativeBuildInputs = [ meson ninja pkgconf ];
  buildInputs = [ libev libffi libxcb libXau libXdmcp xcbutilkeysyms libxkbcommon dbus systemdLibs lua ];
}

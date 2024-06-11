{
  lib,
  stdenv,
  cmake,
  pkg-config,
  pugixml,
  version ? "git",
  doCheck ? false,
}:
stdenv.mkDerivation {
  pname = "hyprwayland-scanner";
  inherit version doCheck;
  src = ../.;

  nativeBuildInputs = [
    cmake
    pkg-config
  ];

  buildInputs = [
    pugixml
  ];

  meta = with lib; {
    homepage = "https://github.com/hyprwm/hyprwayland-scanner";
    description = "A Hyprland version of wayland-scanner in and for C++";
    license = licenses.bsd3;
    platforms = platforms.linux;
  };
}

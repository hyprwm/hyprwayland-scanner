{
  description = "A Hyprland version of wayland-scanner in and for C++";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    systems.url = "github:nix-systems/default-linux";
  };

  outputs = {
    self,
    nixpkgs,
    systems,
  }: let
    inherit (nixpkgs) lib;
    eachSystem = lib.genAttrs (import systems);
    pkgsFor = eachSystem (system:
      import nixpkgs {
        localSystem.system = system;
        overlays = with self.overlays; [hyprwayland-scanner];
      });
    pkgsCrossFor = eachSystem (system: crossSystem:
      import nixpkgs {
        localSystem = system;
        crossSystem = crossSystem;
        overlays = with self.overlays; [hyprwayland-scanner];
      });
    mkDate = longDate: (lib.concatStringsSep "-" [
      (builtins.substring 0 4 longDate)
      (builtins.substring 4 2 longDate)
      (builtins.substring 6 2 longDate)
    ]);

    version = lib.removeSuffix "\n" (builtins.readFile ./VERSION);
  in {
    overlays = {
      default = self.overlays.hyprwayland-scanner;
      hyprwayland-scanner = final: prev: {
        hyprwayland-scanner = final.callPackage ./nix/default.nix {
          stdenv = final.gcc13Stdenv;
          version = version + "+date=" + (mkDate (self.lastModifiedDate or "19700101")) + "_" + (self.shortRev or "dirty");
        };
      };
    };

    packages = eachSystem (system: {
      default = self.packages.${system}.hyprwayland-scanner;
      inherit (pkgsFor.${system}) hyprwayland-scanner;
      hyprwayland-scanner-cross = (pkgsCrossFor.${system} "aarch64-linux").hyprwayland-scanner;
    });

    formatter = eachSystem (system: pkgsFor.${system}.alejandra);
  };
}

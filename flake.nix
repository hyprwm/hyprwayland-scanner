{
  description = "A Hyprland version of wayland-scanner in and for C++";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    systems.url = "github:nix-systems/default-linux";
  };

  outputs =
    {
      self,
      nixpkgs,
      systems,
    }:
    let
      inherit (nixpkgs) lib;
      eachSystem = lib.genAttrs (import systems);
      pkgsFor = eachSystem (
        system:
        import nixpkgs {
          localSystem.system = system;
          overlays = with self.overlays; [ hyprwayland-scanner ];
        }
      );
      pkgsCrossFor = eachSystem (
        system: crossSystem:
        import nixpkgs {
          localSystem = system;
          crossSystem = crossSystem;
          overlays = with self.overlays; [ hyprwayland-scanner ];
        }
      );
    in
    {
      overlays = import ./nix/overlays.nix { inherit lib self; };

      packages = eachSystem (system: {
        default = self.packages.${system}.hyprwayland-scanner;
        inherit (pkgsFor.${system}) hyprwayland-scanner;
        hyprwayland-scanner-cross = (pkgsCrossFor.${system} "aarch64-linux").hyprwayland-scanner;
      });

      formatter = eachSystem (system: pkgsFor.${system}.nixfmt-tree);
    };
}

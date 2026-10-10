{
  description = "Corsair K100 to QMK/VIA: host tooling and firmware development shell";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs = { self, nixpkgs }:
    let
      systems = [ "x86_64-linux" "aarch64-linux" ];
      forAllSystems = f: nixpkgs.lib.genAttrs systems (system: f nixpkgs.legacyPackages.${system});
    in
    {
      devShells = forAllSystems (pkgs: {
        default = pkgs.mkShell {
          name = "k100-qmk";

          nativeBuildInputs = with pkgs; [
            cargo
            rustc
            rustfmt
            clippy
            pkg-config
          ];

          # hidapi builds its Linux hidraw backend against libudev.
          buildInputs = [ pkgs.systemdLibs ];

          packages = with pkgs; [
            python3
            # Reading the firmware images and the updater bundle.
            sigrok-cli
          ];
        };

        # Qt is only needed to build tools/extract-ckb-keymap, which links against
        # ckb-next's own keymap implementation to dump the authoritative layout.
        keymap = pkgs.mkShell {
          name = "k100-qmk-keymap";

          nativeBuildInputs = with pkgs; [ gcc pkg-config ];
          buildInputs = with pkgs; [ qt6.qtbase ];

          packages = [ pkgs.python3 ];
        };

        # Kept separate because the ARM toolchain is a large closure and is only needed
        # once the ChibiOS port work starts.
        firmware = pkgs.mkShell {
          name = "k100-qmk-firmware";

          packages = with pkgs; [
            gcc-arm-embedded
            gnumake
            python3
            dfu-util
          ];
        };
      });
    };
}

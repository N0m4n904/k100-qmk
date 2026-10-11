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

          # hidapi needs libudev for enumeration and libusb for the backend we use; the
          # default hidraw backend cannot drive the K100's Bragi interface.
          buildInputs = with pkgs; [ systemdLibs libusb1 ];

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

        # For qmk/test/build-k100.sh. Separate from `firmware` because the QMK CLI
        # drags in a large Python closure, and the ChibiOS checks do not need it.
        # QMK's build shells out to `qmk` to turn keyboard.json into C, and to
        # `git` for a version string.
        qmk = pkgs.mkShell {
          name = "k100-qmk-qmk";

          packages = with pkgs; [
            qmk
            gcc-arm-embedded
            gnumake
            git
            python3
          ];
        };
      });
    };
}

# devenv.nix — zephyr-edge-platform
#
# Reproducible development shell for the Zephyr RTOS edge device platform.
#
# Phase 1 targets the `native_sim` board, which builds with the host C
# toolchain (no cross-compiler required). This shell therefore provides the
# full Zephyr *host* toolchain: west (Zephyr meta-tool), CMake, Ninja, dtc
# (devicetree compiler), gperf, and the Python packages Zephyr's build system
# imports at configure/build time.
#
# Cross-compilation for the nRF52840 (later phases) needs the Zephyr SDK.
# It is intentionally NOT installed by default because it is ~1 GB and not
# required for native_sim. To enable it, uncomment the `zephyr-sdk` line in
# the Linux branch below, or run:  nix shell nixpkgs#zephyr-sdk
{ pkgs
, lib
, ...
}:

lib.mkMerge [
  # ----- Cross-platform base: orchestration + shell niceties -----
  {
    git-hooks.hooks = {
      nixpkgs-fmt.enable = true;
      deadnix.enable = true;
      trim-trailing-whitespace.enable = true;
      end-of-file-fixer.enable = true;
      check-yaml.enable = true;
      check-merge-conflicts.enable = true;
    };

    packages = with pkgs; [
      git
      gnumake
      gnused
      gnugrep
      coreutils
      curl
      go
      jq
      just # top-level build orchestrator (see ./justfile)
      mosquitto # local e2e broker (E2E_MODE=local)
      shellcheck
      nixfmt-rfc-style
    ];

    env = {
      # Zephyr's CMake glue resolves the SDK via ZEPHYR_BASE; the justfile
      # exports ZEPHYR_BASE to the west-managed checkout at shell entry.
      ZEPHYR_TOOLCHAIN_VARIANT = "host";
    };
  }

  # ----- Python + Zephyr host build toolchain (all platforms) -----
  {
    languages.python = {
      enable = true;
      # `west` is the Zephyr meta-tool used to fetch modules and drive builds;
      # the other modules are imported by Zephyr's build system.
      package = pkgs.python3.withPackages (ps: with ps; [
        west
        pyelftools
        pyyaml
        anytree
        intelhex
        click # MCUboot imgtool signer
        cryptography # MCUboot imgtool signer
        cbor2 # MCUboot imgtool boot records
        packaging
        progress
        psutil
        pyserial
        setuptools
      ]);
    };

    packages = with pkgs; [
      cmake # Zephyr build system generator
      ninja # build backend used by `west build`
      dtc # devicetree compiler (zephyr,make / dtc)
      gperf # perfect-hash generator used by some Zephyr subsystems
      pkg-config
      libusb1 # used by host tooling / flashing probes
    ];
  }

  # ----- Linux-only branch: container + (optional) Zephyr SDK -----
  (lib.mkIf pkgs.stdenv.isLinux {
    packages = with pkgs; [
      docker # local observability stack (Mosquitto/Prometheus/Grafana) in later phases
      # zephyr-sdk  # ~1 GB; uncomment for nRF52840 (ARM) cross-builds. Not needed for native_sim.
    ];

    env = {
      LINUX_DEVENV = "1";
    };
  })
]

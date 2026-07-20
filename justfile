# justfile — zephyr-edge-platform
#
# Top-level orchestration for the Zephyr edge platform monorepo.
# Run inside the devenv shell so west / cmake / ninja / dtc are on PATH:
#
#     devenv shell          # enter the reproducible toolchain shell
#     just init             # fetch the Zephyr workspace (first time)
#     just build            # build the firmware for native_sim
#     just test             # build + run all ztest suites on native_sim
#     just run              # build + run the firmware (Ctrl-C to stop)
#
# The west workspace root is this directory; `firmware/` is the manifest
# repository (west init -l firmware). `west update` clones Zephyr + modules
# as siblings of firmware/.

set shell := ["bash", "-euo", "pipefail", "-c"]

# Board used for all Phase 1 targets (Zephyr v4.x native simulator).
board := "native_sim"

# Default: show available recipes.
default:
    @just --list

# Fetch / refresh the west workspace (Zephyr + modules). Idempotent.
init:
    #!/usr/bin/env bash
    set -euo pipefail
    if [ ! -d .west ]; then
        echo ">> initializing west workspace (manifest: firmware/)"
        west init -l firmware
    fi
    echo ">> west update (fetching Zephyr + modules; first run is large)"
    west update
    echo ">> west workspace ready"

# Build the main firmware application for native_sim.
build: init
    west build -b {{board}} -p auto firmware

# Run the freshly built native_sim firmware binary (Ctrl-C to stop).
run: build
    ./build/zephyr/zephyr.exe

# Build + run every ztest suite on native_sim. Fails fast on the first failure.
test: init
    #!/usr/bin/env bash
    set -euo pipefail
    suites=(bme280 sensor_log power_sched telemetry)
    for suite in "${suites[@]}"; do
        echo ">> ztest: ${suite}"
        west build -b {{board}} -p auto -d "build/test-${suite}" "firmware/tests/${suite}"
        west build -d "build/test-${suite}" -t run
    done
    echo ">> all ztest suites passed"

# Build + run a single ztest suite, e.g. `just test-one bme280`.
test-one suite: init
    west build -b {{board}} -p auto -d "build/test-{{suite}}" "firmware/tests/{{suite}}"
    west build -d "build/test-{{suite}}" -t run

# Remove all build artifacts (keeps the west workspace).
clean:
    rm -rf build build-* twister-out*

# Full reset: build artifacts + vendored west modules (forces a clean re-fetch).
distclean: clean
    rm -rf .west zephyr modules tools bootloader

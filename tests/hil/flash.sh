#!/usr/bin/env bash
# flash.sh — build + flash the firmware (or a ztest suite) to a real nRF52840 DK.
#
# Hardware-in-the-loop (HIL) helper for the zephyr-edge-platform. Builds the
# application for the nRF52840 DK and flashes it via `west flash` using the
# board's onboard debugger (SEGGER J-Link by default).
#
# Prerequisites:
#   - The west workspace is initialized (`just init` from the monorepo root).
#   - The Zephyr host toolchain is available (devenv shell: west, cmake, ninja).
#   - For cross-compiling to Cortex-M4 you need the Zephyr SDK (see the commented
#     zephyr-sdk line in devenv.nix) — native_sim builds do NOT need it.
#   - A flashed nRF52840 DK reachable by the chosen runner (default: jlink).
#
# Usage:
#   ./flash.sh                         # build + flash the main app
#   ./flash.sh -a firmware/tests/ota   # build + flash a specific ztest suite
#   ./flash.sh --runner pyocd          # use a different flash runner
#   ./flash.sh --skip-build            # flash the existing build dir as-is
#   ./flash.sh --sysbuild              # firmware + MCUboot (real swap path)
#
# Options:
#   -b, --board BOARD      Target board (default: nrf52840dk/nrf52840, env BOARD)
#   -a, --app PATH         App/suite to build, relative to repo root
#                          (default: firmware)
#   -d, --build-dir DIR    Build directory (default: build-hil)
#   -r, --runner RUNNER    west flash runner (default: board default = jlink)
#   -s, --skip-build       Skip the build step; flash the existing build dir
#   --sysbuild             Build/flash the app with MCUboot (SB_CONFIG_BOOTLOADER_MCUBOOT)
#   -h, --help             Show this help

set -euo pipefail

# Resolve the monorepo root (this script lives in tests/hil/).
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SELF="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/$(basename "${BASH_SOURCE[0]}")"
cd "$ROOT"

# --- Defaults ---
BOARD="${BOARD:-nrf52840dk/nrf52840}"
APP="firmware"
BUILD_DIR="build-hil"
RUNNER=""
SKIP_BUILD=0
SYSBUILD=0

# --- Argument parsing ---
while [[ $# -gt 0 ]]; do
    case "$1" in
        -b|--board)      BOARD="$2";     shift 2 ;;
        -a|--app)        APP="$2";       shift 2 ;;
        -d|--build-dir)  BUILD_DIR="$2"; shift 2 ;;
        -r|--runner)     RUNNER="$2";    shift 2 ;;
        -s|--skip-build) SKIP_BUILD=1;   shift ;;
        --sysbuild)      SYSBUILD=1;      shift ;;
        -h|--help)
            awk 'NR>1{if(/^set -euo/)exit; if(/^#/)print}' "$SELF" | sed 's/^# \?//'
            exit 0
            ;;
        *)
            echo "ERROR: unknown option: $1" >&2
            echo "       run '$0 --help' for usage" >&2
            exit 2
            ;;
    esac
done

# --- Sanity checks ---
if ! command -v west >/dev/null 2>&1; then
    echo "ERROR: 'west' not found on PATH." >&2
    echo "       Enter the devenv shell first:  devenv shell" >&2
    exit 1
fi
if [[ ! -d .west ]]; then
    echo "ERROR: west workspace not initialized (no .west/)." >&2
    echo "       Run 'just init' from the monorepo root first." >&2
    exit 1
fi
if [[ ! -e "$APP" ]]; then
    echo "ERROR: app path not found: $APP" >&2
    exit 1
fi

# The production app links at slot0 and is only bootable with MCUboot. Enable
# sysbuild automatically unless the caller explicitly targets a test suite.
if [[ "$APP" == "firmware" && "$SYSBUILD" -eq 0 ]]; then
    echo ">> firmware app requires MCUboot; enabling --sysbuild"
    SYSBUILD=1
fi

echo "=== HIL flash ==="
echo "Repo root:  $ROOT"
echo "Board:      $BOARD"
echo "App:        $APP"
echo "Build dir:  $BUILD_DIR"
echo "Runner:     ${RUNNER:-<board default>}"
echo ""

# --- Build ---
BUILD_ARGS=(-b "$BOARD" -p auto -d "$BUILD_DIR")
if [[ "$SYSBUILD" -eq 1 ]]; then
    BUILD_ARGS+=(--sysbuild)
fi
if [[ "$SKIP_BUILD" -eq 0 ]]; then
    echo ">> building $APP for $BOARD (sysbuild=$SYSBUILD) ..."
    west build "${BUILD_ARGS[@]}" "$APP"
else
    echo ">> skipping build (--skip-build)"
    if [[ "$SYSBUILD" -eq 1 ]]; then
        image="$BUILD_DIR/firmware/zephyr/zephyr.hex"
    else
        image="$BUILD_DIR/zephyr/zephyr.hex"
    fi
    if [[ ! -f "$image" && ! -f "${image%.hex}.bin" ]]; then
        echo "WARNING: no $image — flash may fail." >&2
    fi
fi

# --- Flash ---
echo ">> flashing $BUILD_DIR ..."
FLASH_ARGS=(-d "$BUILD_DIR")
if [[ -n "$RUNNER" ]]; then
    FLASH_ARGS+=(--runner "$RUNNER")
fi
west flash "${FLASH_ARGS[@]}"

echo ""
echo ">> flash complete: $APP → $BOARD"
echo "   Reset the board (or it auto-resets) to run the firmware."

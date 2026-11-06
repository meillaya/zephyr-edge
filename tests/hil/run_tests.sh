#!/usr/bin/env bash
# run_tests.sh — build, flash, and run the ztest suites on a real nRF52840 DK.
#
# Hardware-in-the-loop (HIL) test runner for the zephyr-edge-platform. For each
# ztest suite it: builds for the nRF52840 DK, flashes the board, and (if a serial
# TTY is supplied) captures the console output to detect ztest pass/fail.
#
# Ztest result detection: Zephyr prints "PROJECT EXECUTION SUCCESSFUL" on success
# and "PROJECT EXECUTION FAILED" on failure (plus per-TESTSUITE pass/fail lines).
# We treat a captured log as PASS iff it contains "SUCCESSFUL" and no "FAILED".
#
# Prerequisites:
#   - West workspace initialized (`just init`) + Zephyr host toolchain (devenv).
#   - Zephyr SDK for Cortex-M4 cross-compilation (see devenv.nix note).
#   - A connected nRF52840 DK reachable by the chosen runner (default: jlink).
#   - For automatic result capture: the DK's serial port (e.g. /dev/ttyACM0) and
#     a way to read it (pyserial-miniterm, or stty+cat fallback).
#
# Usage:
#   ./run_tests.sh                              # flash all suites; manual verify
#   ./run_tests.sh -t /dev/ttyACM0              # all suites + auto capture/verdict
#   ./run_tests.sh -s telemetry -t /dev/ttyACM0 # one suite, auto verdict
#   ./run_tests.sh --runner pyocd -t /dev/ttyACM0
#
# Options:
#   -b, --board BOARD     Target board (default: nrf52840dk/nrf52840, env BOARD)
#   -s, --suite SUITE     Run a single suite (default: all)
#   -d, --build-dir DIR   Base build dir; per-suite subdirs (default: build-hil)
#   -r, --runner RUNNER   west flash runner (default: board default = jlink)
#   -t, --tty DEV         Serial device to capture ztest output (optional)
#   --baud RATE           Serial baud rate (default: 115200)
#   --timeout SECS        Per-suite capture timeout (default: 60)
#   -h, --help            Show this help

set -euo pipefail

# Resolve the monorepo root (this script lives in tests/hil/).
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SELF="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/$(basename "${BASH_SOURCE[0]}")"
cd "$ROOT"

# The ztest suites that build without a native_sim-specific fixture or an MCUboot
# partition layout. `bme280` asserts emulator fixture values; `ota` needs a
# slot1 partition. Add them back once the board layout/bench test exists.
ALL_SUITES=(sensor_log power_sched telemetry fault_inject)

# --- Defaults ---
BOARD="${BOARD:-nrf52840dk/nrf52840}"
SUITE=""
BUILD_DIR="build-hil"
RUNNER=""
TTY=""
BAUD=115200
TIMEOUT=60

# --- Argument parsing ---
while [[ $# -gt 0 ]]; do
    case "$1" in
        -b|--board)     BOARD="$2";     shift 2 ;;
        -s|--suite)     SUITE="$2";     shift 2 ;;
        -d|--build-dir) BUILD_DIR="$2"; shift 2 ;;
        -r|--runner)    RUNNER="$2";    shift 2 ;;
        -t|--tty)       TTY="$2";       shift 2 ;;
        --baud)         BAUD="$2";      shift 2 ;;
        --timeout)      TIMEOUT="$2";   shift 2 ;;
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

# --- Build the suite list ---
if [[ -n "$SUITE" ]]; then
    SUITES=("$SUITE")
else
    SUITES=("${ALL_SUITES[@]}")
fi

# --- Sanity checks ---
if ! command -v west >/dev/null 2>&1; then
    echo "ERROR: 'west' not found on PATH. Enter the devenv shell first." >&2
    exit 1
fi
if [[ ! -d .west ]]; then
    echo "ERROR: west workspace not initialized (no .west/). Run 'just init' first." >&2
    exit 1
fi
if [[ -n "$TTY" && ! -e "$TTY" ]]; then
    echo "ERROR: serial device not found: $TTY" >&2
    exit 1
fi

FLASH_ARGS=()
if [[ -n "$RUNNER" ]]; then
    FLASH_ARGS+=(--runner "$RUNNER")
fi

echo "=== HIL test run ==="
echo "Repo root:  $ROOT"
echo "Board:      $BOARD"
echo "Suites:     ${SUITES[*]}"
echo "Build dir:  $BUILD_DIR"
echo "Runner:     ${RUNNER:-<board default>}"
echo "Serial TTY: ${TTY:-<none — flash only, verify manually>}"
echo ""

# capture_serial LOG_FILE
# Reads the serial TTY for $TIMEOUT seconds into LOG_FILE using the best
# available tool. Returns 0 regardless (capture is best-effort).
capture_serial() {
    local log="$1"
    if command -v pyserial-miniterm >/dev/null 2>&1; then
        # miniterm exits on timeout via the outer `timeout`; --exit-char avoids
        # needing a keypress. Raw mode, no local echo.
        timeout "$TIMEOUT" pyserial-miniterm "$TTY" "$BAUD" --raw > "$log" 2>/dev/null || true
    elif command -v python3 >/dev/null 2>&1 && python3 -c 'import serial' 2>/dev/null; then
        timeout "$TIMEOUT" python3 -m serial.tools.miniterm "$TTY" "$BAUD" --raw > "$log" 2>/dev/null || true
    else
        # Fallback: configure the port with stty and read with cat.
        stty -F "$TTY" "$BAUD" raw -echo 2>/dev/null || true
        timeout "$TIMEOUT" cat "$TTY" > "$log" 2>/dev/null || true
    fi
}

# verdict LOG_FILE → prints PASS / FAIL / UNKNOWN
verdict() {
    local log="$1"
    if grep -q "PROJECT EXECUTION FAILED" "$log" 2>/dev/null; then
        echo "FAIL"; return
    fi
    if grep -q "PROJECT EXECUTION SUCCESSFUL" "$log" 2>/dev/null; then
        echo "PASS"; return
    fi
    # Newer ztest output uses per-testsuite markers.
    if grep -qiE "TESTSUITE .* failed|FAIL - " "$log" 2>/dev/null; then
        echo "FAIL"; return
    fi
    if grep -qiE "TESTSUITE .* (passed|succeeded)" "$log" 2>/dev/null; then
        echo "PASS"; return
    fi
    echo "UNKNOWN"
}

# --- Run each suite ---
declare -A RESULTS
FAILED=0
for suite in "${SUITES[@]}"; do
    suite_dir="$BUILD_DIR/$suite"
    suite_src="firmware/tests/$suite"

    echo "──────────────────────────────────────────────"
    echo ">> [$suite] building ($suite_src → $BOARD) ..."
    if [[ ! -d "$suite_src" ]]; then
        echo "   SKIP: suite source not found: $suite_src"
        RESULTS[$suite]="SKIP"
        continue
    fi
    west build -b "$BOARD" -p auto -d "$suite_dir" "$suite_src"

    echo ">> [$suite] flashing ..."
    west flash -d "$suite_dir" "${FLASH_ARGS[@]}"

    if [[ -z "$TTY" ]]; then
        echo ">> [$suite] flashed. No --tty given: observe the serial console to verify."
        RESULTS[$suite]="FLASHED"
        continue
    fi

    echo ">> [$suite] capturing serial ($TTY @ $BAUD) for ${TIMEOUT}s ..."
    log_file="$(mktemp "/tmp/hil_${suite}_XXXXXX.log")"
    capture_serial "$log_file"

    result="$(verdict "$log_file")"
    RESULTS[$suite]="$result"
    echo ">> [$suite] result: $result  (log: $log_file)"
    if [[ "$result" == "FAIL" || "$result" == "UNKNOWN" ]]; then
        echo "    --- last 20 log lines ---"
        tail -20 "$log_file" | sed 's/^/    /'
    fi
    if [[ "$result" == "FAIL" || "$result" == "UNKNOWN" ]]; then
        # UNKNOWN means no recognizable pass marker: treat it as not-pass, as
        # documented in tests/hil/README.md.
        FAILED=1
    fi
done

# --- Summary ---
echo ""
echo "=== HIL summary ($BOARD) ==="
for suite in "${SUITES[@]}"; do
    printf '  %-14s %s\n' "$suite" "${RESULTS[$suite]:-?}"
done

if [[ -z "$TTY" ]]; then
    echo ""
    echo "Note: no --tty was provided, so suites were flashed but not auto-verified."
    echo "      Re-run with '-t /dev/ttyACM0' (your DK's serial port) for verdicts."
fi

if [[ "$FAILED" -ne 0 ]]; then
    echo ""
    echo "RESULT: FAIL (one or more suites failed)"
    exit 1
fi
echo ""
echo "RESULT: OK"

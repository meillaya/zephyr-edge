#!/usr/bin/env bash
# power_bench.sh — Power-state benchmark for zephyr-edge-platform (native_sim).
#
# Runs the native_sim firmware binary for a configurable duration, captures log
# output, parses power-state counters from the periodic "power:" log lines, and
# produces a markdown report with time-in-state percentages.
#
# Usage:
#   ./power_bench.sh [OPTIONS]
#
# Options:
#   -d, --duration SECS   Run duration in seconds (default: 10)
#   -b, --binary PATH     Path to native_sim binary (default: auto-detect)
#   -o, --output PATH     Output markdown report path (default: power_report.md)
#   -h, --help            Show this help
#
# The script expects the firmware to be built with:
#   west build -b native_sim firmware/
#
# Log parsing: looks for lines matching:
#   "power: cycles=N active=Xms idle=Yms sleep=Zms"
# and extracts the cumulative counters from the last occurrence.
#
# Output: a markdown table with state, time (ms), and percentage of total.

set -euo pipefail

# --- Defaults ---
DURATION=10
BINARY=""
OUTPUT="power_report.md"

# --- Argument parsing ---
while [[ $# -gt 0 ]]; do
    case "$1" in
        -d|--duration)
            DURATION="$2"
            shift 2
            ;;
        -b|--binary)
            BINARY="$2"
            shift 2
            ;;
        -o|--output)
            OUTPUT="$2"
            shift 2
            ;;
        -h|--help)
            head -30 "$0" | grep '^#' | sed 's/^# \?//'
            exit 0
            ;;
        *)
            echo "Unknown option: $1" >&2
            exit 1
            ;;
    esac
done

# --- Auto-detect binary ---
if [[ -z "$BINARY" ]]; then
    # Common build output locations
    for candidate in \
        "build/zephyr/zephyr.exe" \
        "firmware/build/zephyr/zephyr.exe" \
        "../build/zephyr/zephyr.exe" \
        "zephyr-edge-platform/build/zephyr/zephyr.exe"; do
        if [[ -f "$candidate" ]]; then
            BINARY="$candidate"
            break
        fi
    done
fi

if [[ -z "$BINARY" || ! -f "$BINARY" ]]; then
    echo "ERROR: native_sim binary not found. Build with:" >&2
    echo "  west build -b native_sim firmware/" >&2
    echo "Or specify the path with --binary" >&2
    exit 1
fi

echo "=== Power Benchmark ==="
echo "Binary:   $BINARY"
echo "Duration: ${DURATION}s"
echo "Output:   $OUTPUT"
echo ""

# --- Run native_sim and capture output ---
LOG_FILE=$(mktemp /tmp/power_bench_XXXXXX.log)
trap 'rm -f "$LOG_FILE"' EXIT

echo "Running native_sim for ${DURATION}s..."
timeout "${DURATION}s" "$BINARY" > "$LOG_FILE" 2>&1 || true

# --- Parse power-state counters ---
# Look for the last "power:" line with cumulative counters.
# Format: "power: cycles=N active=Xms idle=Yms sleep=Zms (dropped=D)"
POWER_LINE=$(grep -E 'power:.*cycles=.*active=.*idle=.*sleep=' "$LOG_FILE" | tail -1 || true)

if [[ -z "$POWER_LINE" ]]; then
    echo "ERROR: no power-state log lines found in the captured output." >&2
    echo "Build with firmware/boards/power_bench.conf (short cycles) or run longer:" >&2
    echo "  just power-bench 10" >&2
    echo "Last output:" >&2
    tail -20 "$LOG_FILE" >&2
    exit 1
fi

echo "Found power line: $POWER_LINE"
CYCLES=$(echo "$POWER_LINE" | grep -oP 'cycles=\K[0-9]+' || echo "0")
ACTIVE_MS=$(echo "$POWER_LINE" | grep -oP 'active=\K[0-9]+' || echo "0")
IDLE_MS=$(echo "$POWER_LINE" | grep -oP 'idle=\K[0-9]+' || echo "0")
SLEEP_MS=$(echo "$POWER_LINE" | grep -oP 'sleep=\K[0-9]+' || echo "0")

# --- Calculate percentages ---
TOTAL_MS=$((ACTIVE_MS + IDLE_MS + SLEEP_MS))

if [[ $TOTAL_MS -eq 0 ]]; then
    ACTIVE_PCT="0.00"
    IDLE_PCT="0.00"
    SLEEP_PCT="0.00"
else
    ACTIVE_PCT=$(awk "BEGIN {printf \"%.2f\", ($ACTIVE_MS / $TOTAL_MS) * 100}")
    IDLE_PCT=$(awk "BEGIN {printf \"%.2f\", ($IDLE_MS / $TOTAL_MS) * 100}")
    SLEEP_PCT=$(awk "BEGIN {printf \"%.2f\", ($SLEEP_MS / $TOTAL_MS) * 100}")
fi

# --- Generate markdown report ---
cat > "$OUTPUT" << REPORT
# Power Benchmark Report

**Date:** $(date -u +"%Y-%m-%d %H:%M:%S UTC")
**Binary:** \`$BINARY\`
**Duration:** ${DURATION}s
**Cycles:** $CYCLES

## Time-in-State Summary

| State   | Time (ms) | Percentage |
|---------|-----------|------------|
| ACTIVE  | $ACTIVE_MS | ${ACTIVE_PCT}% |
| IDLE    | $IDLE_MS   | ${IDLE_PCT}% |
| SLEEP   | $SLEEP_MS  | ${SLEEP_PCT}% |
| **Total** | **$TOTAL_MS** | **100.00%** |

## Notes

- Measurements are software-level power-state time distribution on native_sim.
- Actual power consumption (mW) requires hardware measurement (documented limitation).
- The power scheduler cycles ACTIVE → IDLE → SLEEP with configurable durations
  (CONFIG_POWER_ACTIVE_MS, CONFIG_POWER_IDLE_MS, CONFIG_POWER_SLEEP_MS).
- just power-bench builds firmware/boards/power_bench.conf (500ms/100ms/500ms)
  so a short run produces samples; the production defaults are 5s/100ms/5s.

## Raw Log (last 20 lines)

\`\`\`
$(tail -20 "$LOG_FILE")
\`\`\`
REPORT

echo ""
echo "=== Report generated: $OUTPUT ==="
echo ""
echo "Summary:"
echo "  ACTIVE: ${ACTIVE_MS}ms (${ACTIVE_PCT}%)"
echo "  IDLE:   ${IDLE_MS}ms (${IDLE_PCT}%)"
echo "  SLEEP:  ${SLEEP_MS}ms (${SLEEP_PCT}%)"
echo "  Total:  ${TOTAL_MS}ms over ${CYCLES} cycles"

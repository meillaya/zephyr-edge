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
    suites=(bme280 sensor_log power_sched telemetry ota fault_inject)
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

# Run the power benchmark script (Phase 3).
power-bench duration="10": build
    #!/usr/bin/env bash
    set -euo pipefail
    ./tests/scripts/power_bench.sh --duration {{duration}} --binary ./build/zephyr/zephyr.exe --output power_report.md

# Remove all build artifacts (keeps the west workspace).
clean:
    rm -rf build build-* twister-out*

# Full reset: build artifacts + vendored west modules (forces a clean re-fetch).
distclean: clean
    rm -rf .west zephyr modules tools bootloader

# ── Phase 4: e2e demo ─────────────────────────────────────────────────

# Run the full end-to-end demo: infra stack + fleet simulator + live metrics.
# Starts Mosquitto → cloud-ingest → Prometheus → Grafana, then launches the
# Go fleet simulator with N synthetic devices publishing CBOR telemetry.
# Ctrl-C stops the fleet sim; the infra stack is torn down on exit.
demo devices="10":
    #!/usr/bin/env bash
    set -euo pipefail

    INFRA_DIR="$(pwd)/infra"
    FLEET_SIM="$(pwd)/tests/fleet-sim"

    cleanup() {
        echo ""
        echo ">> stopping fleet simulator..."
        kill "$FLEET_PID" 2>/dev/null || true
        wait "$FLEET_PID" 2>/dev/null || true
        echo ">> tearing down infra stack..."
        docker compose -f "${INFRA_DIR}/docker-compose.yml" down -v
        echo ">> demo complete."
    }
    trap cleanup EXIT INT TERM

    # 1. Start the observability stack.
    echo ">> starting infra stack (mosquitto → cloud-ingest → prometheus → grafana)..."
    docker compose -f "${INFRA_DIR}/docker-compose.yml" up -d --build

    # 2. Wait for cloud-ingest to be healthy.
    echo ">> waiting for cloud-ingest to be healthy..."
    for i in $(seq 1 40); do
        if curl -sf http://localhost:9090/health >/dev/null 2>&1; then
            echo "   cloud-ingest: healthy"
            break
        fi
        if [ "$i" -eq 40 ]; then
            echo "ERROR: cloud-ingest did not become healthy" >&2
            docker compose -f "${INFRA_DIR}/docker-compose.yml" logs cloud-ingest
            exit 1
        fi
        sleep 2
    done

    # 3. Launch the fleet simulator.
    echo ">> starting fleet simulator with {{devices}} devices..."
    if [ ! -f "${FLEET_SIM}/main.go" ]; then
        echo "ERROR: fleet simulator not found at ${FLEET_SIM}/main.go" >&2
        echo "       Run 'just demo' after the fleet-sim has been created (Phase 4)." >&2
        exit 1
    fi
    (cd "${FLEET_SIM}" && go run main.go -n {{devices}}) &
    FLEET_PID=$!

    # 4. Wait for telemetry to appear in Prometheus.
    echo ">> waiting for telemetry metrics in Prometheus..."
    for i in $(seq 1 30); do
        count=$(curl -sf 'http://localhost:9091/api/v1/query?query=telemetry_messages_total' 2>/dev/null \
            | jq '[.data.result[] | select(.metric.device_id != "")] | length' 2>/dev/null || echo "0")
        if [ "$count" -gt 0 ] 2>/dev/null; then
            echo "   telemetry flowing from ${count} device(s)"
            break
        fi
        if [ "$i" -eq 30 ]; then
            echo "WARNING: no telemetry metrics after 60s — check fleet-sim logs" >&2
        fi
        sleep 2
    done

    # 5. Print dashboard URLs.
    echo ""
    echo "╔══════════════════════════════════════════════════════════╗"
    echo "║  Zephyr Edge Platform — E2E Demo Running                ║"
    echo "╠══════════════════════════════════════════════════════════╣"
    echo "║  Grafana:      http://localhost:3000  (admin / admin)   ║"
    echo "║  Prometheus:   http://localhost:9091                    ║"
    echo "║  Cloud ingest: http://localhost:9090/metrics            ║"
    echo "║  MQTT broker:  localhost:1883                           ║"
    echo "║  Fleet sim:    {{devices}} devices, PID ${FLEET_PID}              ║"
    echo "╠══════════════════════════════════════════════════════════╣"
    echo "║  Press Ctrl-C to stop the demo and tear down the stack. ║"
    echo "╚══════════════════════════════════════════════════════════╝"
    echo ""

    # 6. Wait for the fleet sim to exit (or Ctrl-C).
    wait "$FLEET_PID"

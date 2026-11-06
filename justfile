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

# Board used for all firmware targets. native_sim/native/64 avoids requiring
# 32-bit multilib libraries on the host; override with BOARD=... when needed.
board := env("BOARD", "native_sim/native/64")

# Container runtime used by `just demo`; set COMPOSE="podman compose" if needed.
compose := env("COMPOSE", "docker compose")

# Default: show available recipes.
default:
    @just --list

# Fetch / refresh the west workspace (Zephyr + modules). Idempotent and
# network-free once the workspace exists; use `just update` to refresh it.
init:
    #!/usr/bin/env bash
    set -euo pipefail
    if [ ! -d .west ]; then
        echo ">> initializing west workspace (manifest: firmware/)"
        west init -l firmware
    fi
    if [ ! -d zephyr ]; then
        echo ">> west update (fetching Zephyr + modules; first run is large)"
        west update
    fi
    echo ">> west workspace ready"

# Refresh an existing west workspace (network operation).
update:
    west update

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

# Run the power benchmark on a short-cycle build so a 10s run yields samples.
# Usage: `just power-bench` (default 10s) or `just power-bench 30`.
power-bench duration="10": init
    #!/usr/bin/env bash
    set -euo pipefail
    west build -b {{board}} -p auto -d build/power-bench firmware \
        -- -DEXTRA_CONF_FILE="$(pwd)/firmware/boards/power_bench.conf"
    ./tests/scripts/power_bench.sh --duration {{duration}} \
        --binary ./build/power-bench/zephyr/zephyr.exe \
        --output power_report.md

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
    FLEET_PID=""

    cleanup() {
        echo ""
        if [ -n "$FLEET_PID" ]; then
            echo ">> stopping fleet simulator..."
            kill "$FLEET_PID" 2>/dev/null || true
            wait "$FLEET_PID" 2>/dev/null || true
        fi
        echo ">> tearing down infra stack..."
        {{compose}} -f "${INFRA_DIR}/docker-compose.yml" down -v
        echo ">> demo complete."
    }
    trap cleanup EXIT INT TERM

    # 1. Start the observability stack.
    echo ">> starting infra stack (mosquitto → cloud-ingest → prometheus → grafana)..."
    {{compose}} -f "${INFRA_DIR}/docker-compose.yml" up -d --build

    # 2. Wait for cloud-ingest to be healthy.
    echo ">> waiting for cloud-ingest to be healthy..."
    healthy=0
    for i in $(seq 1 40); do
        if curl -sf http://localhost:9090/health >/dev/null 2>&1; then
            echo "   cloud-ingest: healthy"
            healthy=1
            break
        fi
        sleep 2
    done
    if [ "$healthy" -ne 1 ]; then
        echo "ERROR: cloud-ingest did not become healthy" >&2
        {{compose}} -f "${INFRA_DIR}/docker-compose.yml" logs cloud-ingest
        exit 1
    fi

    # 3. Launch the fleet simulator.
    echo ">> starting fleet simulator with {{devices}} devices..."
    (cd "${FLEET_SIM}" && go run . -n {{devices}}) &
    FLEET_PID=$!

    # 4. Wait for telemetry to appear in Prometheus.
    echo ">> waiting for telemetry metrics in Prometheus..."
    flowing=0
    for i in $(seq 1 30); do
        count=$(curl -sf 'http://localhost:9091/api/v1/query?query=telemetry_messages_total' 2>/dev/null \
            | jq '[.data.result[] | select(.metric.device_id != "")] | length' 2>/dev/null || echo "0")
        if [ "$count" -gt 0 ] 2>/dev/null; then
            echo "   telemetry flowing from ${count} device(s)"
            flowing=1
            break
        fi
        sleep 2
    done
    if [ "$flowing" -ne 1 ]; then
        echo "ERROR: no telemetry metrics after 60s — check fleet-sim logs" >&2
        exit 1
    fi

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

# ── Local quality gates ──────────────────────────────────────────────

# Build + test firmware, all Go modules, and the topic/wire contract.
check: init
    #!/usr/bin/env bash
    set -euo pipefail
    just build
    just test
    (cd cloud && go test -race -count=1 ./... && go vet ./...)
    (cd tests/fleet-sim && go test -race -count=1 ./...)
    (cd tests/ota-sim && go test -count=1 ./...)
    ./tests/scripts/check-contract.sh
    echo ">> all local checks passed"

# Assert firmware/cloud/compose/docs agree on topics, frames, and links.
contract:
    ./tests/scripts/check-contract.sh

# Non-interactive pipeline test (Docker by default; E2E_MODE=local for the sandbox).
e2e:
    ./tests/e2e/run.sh

# Publish an OTA image to a running device over MQTT.
ota-sim image device="native-sim-001" broker="tcp://localhost:1883":
    cd tests/ota-sim && go run . -broker {{broker}} -device {{device}} -image {{image}}

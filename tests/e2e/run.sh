#!/usr/bin/env bash
# run.sh — non-interactive pipeline test.
#
# E2E_MODE=docker (default): start the compose stack, publish with fleet-sim,
# assert the cloud metrics endpoint observed valid telemetry, then tear down.
# E2E_MODE=local: start mosquitto + the cloud binary directly (for hosts where
# nested Docker is not available), publish fleet-sim + the firmware's golden
# CBOR payload, and assert both land in /metrics.
#
# The broker uses TLS + password authentication in both modes. The committed
# certs/credentials are development-only (see infra/tls/README.md).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
MODE="${E2E_MODE:-docker}"
TMP="$(mktemp -d /tmp/zephyr-edge-e2e.XXXXXX)"
CLOUD_PID=""
MOSQ_PID=""
COMPOSE_ARGS=(-f "$ROOT/infra/docker-compose.yml")
read -r -a COMPOSE_CMD <<< "${COMPOSE:-docker compose}"
CA="$ROOT/infra/tls/ca.crt"

cleanup() {
    if [[ -n "$CLOUD_PID" ]]; then kill "$CLOUD_PID" 2>/dev/null || true; fi
    if [[ -n "$MOSQ_PID" ]]; then kill "$MOSQ_PID" 2>/dev/null || true; fi
    if [[ "$MODE" == "docker" ]]; then
        "${COMPOSE_CMD[@]}" "${COMPOSE_ARGS[@]}" down -v >/dev/null 2>&1 || true
    fi
    rm -rf "$TMP"
}
trap cleanup EXIT

wait_metrics() {
    local pattern="$1" deadline=$((SECONDS + 60))
    while (( SECONDS < deadline )); do
        if curl -sf http://127.0.0.1:9090/metrics 2>/dev/null | grep -Fq "$pattern"; then
            echo "   observed: $pattern"
            return 0
        fi
        sleep 1
    done
    echo "ERROR: metric not observed within 60s: $pattern" >&2
    curl -s http://127.0.0.1:9090/metrics 2>/dev/null | head -40 >&2 || true
    return 1
}

run_fleet() {
    (cd "$ROOT/tests/fleet-sim" && go run . -n 5 -duration 15s \
        -tls -ca "$CA" -username edge-device -password change-me)
}

if [[ "$MODE" == "docker" ]]; then
    command -v "${COMPOSE_CMD[0]}" >/dev/null || {
        echo "ERROR: compose command not found: ${COMPOSE_CMD[0]}" >&2
        exit 1
    }
    if [[ "${COMPOSE_CMD[0]}" == "docker" ]]; then
        docker info >/dev/null 2>&1 || { echo "ERROR: docker daemon not reachable" >&2; exit 1; }
    fi

    echo ">> starting compose stack"
    "${COMPOSE_CMD[@]}" "${COMPOSE_ARGS[@]}" up -d --build

    echo ">> waiting for cloud-ingest readiness"
    for _ in $(seq 1 60); do
        if curl -sf http://127.0.0.1:9090/health >/dev/null 2>&1; then break; fi
        sleep 1
    done
    curl -sf http://127.0.0.1:9090/health >/dev/null || {
        "${COMPOSE_CMD[@]}" "${COMPOSE_ARGS[@]}" logs cloud-ingest >&2 || true
        echo "ERROR: cloud-ingest not ready" >&2
        exit 1
    }
    for url in http://127.0.0.1:9091/-/healthy http://127.0.0.1:3000/api/health; do
        for _ in $(seq 1 60); do
            if curl -sf "$url" >/dev/null 2>&1; then break; fi
            sleep 1
        done
        curl -sf "$url" >/dev/null || { echo "ERROR: $url not healthy" >&2; exit 1; }
    done

    echo ">> publishing fleet telemetry over TLS"
    run_fleet

    wait_metrics 'telemetry_messages_total{device_id="fleet-sim-1"}'
    echo "e2e ok (docker)"
    exit 0
fi

[[ "$MODE" == "local" ]] || { echo "ERROR: E2E_MODE must be docker or local" >&2; exit 2; }
command -v mosquitto >/dev/null || { echo "ERROR: mosquitto not found" >&2; exit 1; }
command -v mosquitto_pub >/dev/null || { echo "ERROR: mosquitto_pub not found" >&2; exit 1; }
command -v curl >/dev/null || { echo "ERROR: curl not found" >&2; exit 1; }

# The nixpkgs mosquitto package does not create the distro "mosquitto" user;
# run as the current user so local mode works on both NixOS and Debian.
cat > "$TMP/mosquitto.conf" <<MQTT
listener 8883 127.0.0.1
allow_anonymous false
password_file $ROOT/infra/mosquitto/config/passwd
cafile $CA
certfile $ROOT/infra/tls/server.crt
keyfile $ROOT/infra/tls/server.key
tls_version tlsv1.2
persistence false
log_dest none
user $(id -un)
MQTT
mosquitto -c "$TMP/mosquitto.conf" >"$TMP/mosquitto.log" 2>&1 &
MOSQ_PID=$!

echo ">> building cloud-ingest"
(cd "$ROOT/cloud" && go build -o "$TMP/cloud-ingest" .)
MQTT_BROKER=tls://127.0.0.1:8883 MQTT_TLS_CA="$CA" \
    MQTT_TLS_SERVER_NAME=localhost MQTT_TOPIC='telemetry/+' \
    MQTT_USERNAME=edge-ingest MQTT_PASSWORD=change-me \
    HTTP_ADDR=:9090 "$TMP/cloud-ingest" >"$TMP/cloud.log" 2>&1 &
CLOUD_PID=$!

echo ">> waiting for cloud-ingest readiness"
for _ in $(seq 1 60); do
    if curl -sf http://127.0.0.1:9090/health >/dev/null 2>&1; then break; fi
    sleep 1
done
curl -sf http://127.0.0.1:9090/health >/dev/null || {
    cat "$TMP/cloud.log" >&2 || true
    cat "$TMP/mosquitto.log" >&2 || true
    echo "ERROR: cloud-ingest not ready" >&2
    exit 1
}

echo ">> publishing fleet telemetry over TLS"
run_fleet
wait_metrics 'telemetry_messages_total{device_id="fleet-sim-1"}'

echo ">> publishing the firmware golden CBOR payload over TLS"
mosquitto_pub -h 127.0.0.1 -p 8883 --cafile "$CA" \
    -u edge-device -P change-me -q 1 -t telemetry/native-sim-001 \
    -f "$ROOT/cloud/telemetry/testdata/firmware-v1.cbor"
wait_metrics 'telemetry_messages_total{device_id="native-sim-001"}'
wait_metrics 'telemetry_last_seen_timestamp_seconds{device_id="native-sim-001"}'

if grep -q 'topic/device mismatch' "$TMP/cloud.log"; then
    echo "ERROR: topic/device mismatch in e2e logs" >&2
    exit 1
fi

echo "e2e ok (local)"

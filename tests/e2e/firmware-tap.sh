#!/usr/bin/env bash
# firmware-tap.sh — exercise the native_sim firmware's MQTT/TLS path end-to-end.
#
# Creates a rootless user+network namespace with a TAP interface, starts a
# TLS+password Mosquitto inside it, and runs the native_sim firmware. The
# assertion is the firmware's own logs: TLS handshake, authenticated CONNECT,
# and at least one published telemetry message.
#
# This is a local/bench check, not a CI job: it needs /dev/net/tun and
# CAP_NET_ADMIN inside a user namespace.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
APP="${FIRMWARE_BINARY:-$ROOT/build/zephyr/zephyr.exe}"
TMP="$(mktemp -d /tmp/zephyr-edge-fw-tap.XXXXXX)"
LOG="$TMP/firmware.log"
BROKER_LOG="$TMP/mosquitto.log"
trap 'rm -rf "$TMP"' EXIT

[[ -x "$APP" ]] || { echo "ERROR: firmware binary not found: $APP (run 'just build')" >&2; exit 1; }
command -v unshare >/dev/null || { echo "ERROR: unshare not found" >&2; exit 1; }
command -v mosquitto >/dev/null || { echo "ERROR: mosquitto not found" >&2; exit 1; }
[[ -c /dev/net/tun ]] || { echo "ERROR: /dev/net/tun not available" >&2; exit 1; }

cat > "$TMP/mosquitto.conf" <<MQTT
listener 8883 0.0.0.0
allow_anonymous false
password_file $ROOT/infra/mosquitto/config/passwd
cafile $ROOT/infra/tls/ca.crt
certfile $ROOT/infra/tls/server.crt
keyfile $ROOT/infra/tls/server.key
user root
log_dest stdout
MQTT

echo ">> running firmware TLS/TAP check (native_sim ↔ Mosquitto:8883)"
unshare -rn bash -c '
set -euo pipefail
ip link set lo up
ip tuntap add dev zeth mode tap
ip addr add 192.0.2.2/24 dev zeth
ip link set zeth up
mosquitto -c "$1" > "$2" 2>&1 &
broker=$!
sleep 1
timeout 18 "$3" > "$4" 2>&1 || true
kill "$broker" 2>/dev/null || true
wait "$broker" 2>/dev/null || true
' -- "$TMP/mosquitto.conf" "$BROKER_LOG" "$APP" "$LOG"

grep -q 'mqtt connected to 192.0.2.2:8883' "$LOG" || {
    echo "ERROR: firmware did not complete the TLS MQTT connection" >&2
    tail -30 "$LOG" >&2
    exit 1
}
grep -q '\[telemetry\] published' "$LOG" || {
    echo "ERROR: firmware did not publish telemetry" >&2
    tail -30 "$LOG" >&2
    exit 1
}
grep -q "u'edge-device'" "$BROKER_LOG" || {
    echo "ERROR: broker did not authenticate edge-device" >&2
    tail -20 "$BROKER_LOG" >&2
    exit 1
}

echo "firmware TLS/TAP ok: authenticated MQTT publish over TLS"

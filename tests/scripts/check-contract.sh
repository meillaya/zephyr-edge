#!/usr/bin/env bash
# check-contract.sh — guard the firmware/cloud/docs topic + wire contract.
#
# Run from anywhere; it resolves the repo root from this script's location.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT"

fail() {
    echo "CONTRACT FAIL: $*" >&2
    exit 1
}

require_grep() {
    local pattern="$1" file="$2" what="$3"
    grep -Eq "$pattern" "$file" || fail "${what}: ${file} does not match /${pattern}/"
}

# ── Telemetry topic contract ─────────────────────────────────────────
require_grep 'default "telemetry/%s"' firmware/Kconfig.appdefs "firmware telemetry topic"
require_grep 'envOr\("MQTT_TOPIC", "telemetry/\+"\)' cloud/main.go "cloud default topic"
require_grep 'MQTT_TOPIC: "telemetry/\+"' infra/docker-compose.yml "compose topic"
require_grep 'telemetry/<device_id>' docs/DATA_MODEL.md "data model topic"
require_grep '"telemetry/" \+ id' tests/fleet-sim/main.go "fleet-sim topic"
require_grep 'topicFilter = "telemetry/\+"' cloud/main.go "cloud topic filter"

# ── OTA topic + frame contract ───────────────────────────────────────
require_grep 'default "cmd/%s/ota"' firmware/Kconfig.appdefs "firmware OTA topic"
require_grep 'cmd/<device_id>/ota' docs/DATA_MODEL.md "data model OTA topic"
require_grep 'opStart  byte = 0x01' tests/ota-sim/frame.go "ota-sim start opcode"
require_grep 'OTA_FRAME_START = 0x01' firmware/src/ota/ota_update.h "firmware start opcode"

# ── Shared golden CBOR fixture ───────────────────────────────────────
golden="cloud/telemetry/testdata/firmware-v1.cbor"
[[ -f "$golden" ]] || fail "missing golden payload: $golden"
size=$(wc -c < "$golden")
[[ "$size" -eq 75 ]] || fail "golden payload size is $size, want 75"
grep -q '0xbf, 0x00, 0x01, 0x01' firmware/tests/telemetry/src/main.c \
    || fail "firmware golden byte test is missing"

# ── Stale names / references ─────────────────────────────────────────
live=(README.md firmware cloud infra tests docs/DATA_MODEL.md docs/DEMO.md docs/OTA.md docs/ARCHITECTURE.md)
if grep -RInE 'devices/%s/(telemetry|cmd)|devices/<device_id>|\.omx/|^bootloader/' \
        "${live[@]}" 2>/dev/null | grep -v 'check-contract.sh' >/dev/null; then
    grep -RInE 'devices/%s/(telemetry|cmd)|devices/<device_id>|\.omx/|^bootloader/' \
        "${live[@]}" 2>/dev/null | grep -v 'check-contract.sh' >&2 || true
    fail "stale topic/workspace references found"
fi

# ── README stays simple and its local links exist ────────────────────
[[ -f LICENSE ]] || fail "missing LICENSE"
[[ -f docs/diagrams/architecture.png ]] || fail "missing README architecture diagram"

while IFS= read -r link; do
    [[ "$link" =~ ^https?:// ]] && continue
    path="${link%%#*}"
    [[ -z "$path" ]] && continue
    [[ -e "$path" ]] || fail "README link target missing: $path"
done < <(grep -oE '\]\([^)]+' README.md | sed 's/^](//')

echo "contract ok: topics, OTA frames, golden payload, README links"

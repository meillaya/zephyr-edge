#!/usr/bin/env bash
# Regenerate the local-development CA/server certificate, MQTT password file,
# and the firmware's embedded CA header.
#
# WARNING: these are development credentials committed to the repo. Rotate and
# keep them out of source control for any shared or production deployment.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"

MOSQUITTO_PASSWD="${MOSQUITTO_PASSWD:-mosquitto_passwd}"
command -v "$MOSQUITTO_PASSWD" >/dev/null || {
    echo "ERROR: mosquitto_passwd not on PATH; set MOSQUITTO_PASSWD=/path/to/mosquitto_passwd" >&2
    exit 1
}

openssl genrsa -out ca.key 4096
openssl req -x509 -new -nodes -key ca.key -sha256 -days 3650 \
    -subj "/CN=zephyr-edge dev CA" \
    -addext "basicConstraints=critical,CA:TRUE" \
    -addext "keyUsage=critical,keyCertSign,cRLSign" \
    -out ca.crt

openssl genrsa -out server.key 2048
openssl req -new -key server.key -subj "/CN=mosquitto" -out server.csr
cat > server.ext <<'EXT'
subjectAltName=DNS:mosquitto,DNS:localhost,DNS:broker,IP:127.0.0.1,IP:192.0.2.2
basicConstraints=CA:FALSE
keyUsage=digitalSignature,keyEncipherment
extendedKeyUsage=serverAuth
EXT
openssl x509 -req -in server.csr -CA ca.crt -CAkey ca.key -CAcreateserial \
    -out server.crt -days 3650 -sha256 -extfile server.ext
rm -f server.csr server.ext ca.srl
chmod 600 ca.key server.key

"$MOSQUITTO_PASSWD" -b -c ../mosquitto/config/passwd edge-device change-me
"$MOSQUITTO_PASSWD" -b ../mosquitto/config/passwd edge-ingest change-me
chmod 644 ../mosquitto/config/passwd

python3 - <<'PY'
from pathlib import Path
pem = Path("ca.crt").read_bytes()
out = [
    "/* Generated from infra/tls/ca.crt by infra/tls/generate.sh. */",
    "#ifndef ZEPHYR_EDGE_TELEMETRY_CA_CERT_H_",
    "#define ZEPHYR_EDGE_TELEMETRY_CA_CERT_H_",
    "",
    "static const unsigned char telemetry_ca_cert[] = {",
]
pem += b"\x00"
for i in range(0, len(pem), 12):
    out.append("    " + ", ".join(f"0x{b:02x}" for b in pem[i:i + 12]) + ",")
out += ["};", "", "#define TELEMETRY_CA_CERT_LEN (sizeof(telemetry_ca_cert))", "", "#endif", ""]
Path("../../firmware/src/telemetry/telemetry_ca_cert.h").write_text(
    "\n".join(out)
)
PY

echo ">> regenerated dev TLS material and firmware CA header"

# Development TLS material

Committed **local-development only** credentials for MQTT over TLS:

| File | Purpose |
|------|---------|
| `ca.crt` / `ca.key` | Dev CA; firmware/cloud trust `ca.crt`. |
| `server.crt` / `server.key` | Mosquitto server certificate (SAN: `mosquitto`, `localhost`, `broker`, `127.0.0.1`, `192.0.2.2`). |
| `../../mosquitto/config/passwd` | MQTT users `edge-device` and `edge-ingest` (password `change-me`). |

The firmware embeds the CA as `firmware/src/telemetry/telemetry_ca_cert.h`.

**These keys are in git and must never be used outside localhost/CI.** For a
real deployment, generate a private CA, keep the keys in a secret store, rotate
the MQTT password, and set `MQTT_TLS_CA`/`MQTT_TLS_SERVER_NAME` accordingly.

Regenerate everything (requires `openssl`, `mosquitto_passwd`, Python):

```bash
./generate.sh
# then rebuild the firmware/cloud/fleet-sim images
```

`mosquitto_passwd` may not be on PATH on NixOS; pass its store path:

```bash
MOSQUITTO_PASSWD=$(nix shell nixpkgs#mosquitto -c command -v mosquitto_passwd) ./generate.sh
```

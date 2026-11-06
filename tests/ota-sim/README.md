# tests/ota-sim — host OTA publisher

Publishes the firmware's binary OTA frames (`start`, chunked `data`, `finish`,
`abort`) to `cmd/<device_id>/ota` over MQTT, matching
[`../../docs/OTA.md`](../../docs/OTA.md).

```bash
# From the repo root, with the TLS dev stack running:
just ota-sim build-hil/firmware/zephyr/zephyr.signed.bin nrf52840-001

# Or directly:
cd tests/ota-sim
go run . -broker tls://localhost:8883 -ca ../../infra/tls/ca.crt \
  -username edge-device -password change-me \
  -device native-sim-001 -image /path/image.bin
```

Defaults are `tls://localhost:8883`, `../../infra/tls/ca.crt`,
`edge-device`/`change-me` (development credentials). Pass
`-tls=false -broker tcp://localhost:1883` only for an isolated broker.

The publisher stops after `finish`; there is no device status topic yet, so
confirm completion on the device console or after reboot on real MCUboot.

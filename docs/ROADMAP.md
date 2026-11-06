# Roadmap

The current tree is a working `native_sim` reference platform: firmware builds,
all ztests run, the cloud pipeline is covered by unit + e2e tests, OTA and fault
injection are wired and tested, and a reproducible sandbox exists.

## Current state (v1.0 remediation)

- [x] `native_sim/native/64` default; host toolchain builds without multilib.
- [x] Telemetry: ring buffer, CBOR, QoS1, disconnect-safe draining, sleep-window
  MQTT servicing.
- [x] OTA: documented binary frames, full session/reject ztests, resubscribe,
  flash-corruption hook, host publisher.
- [x] Cloud: topic/device validation, `/health` readiness, golden CBOR fixture,
  main-package tests, new metrics.
- [x] Contract/e2e/CI/power-bench/HIL script fixes.
- [x] README stays simple; architecture, data-model, OTA, and audit docs added;
  Excalidraw diagrams.
- [x] MIT license.

## Next: hardware validation

1. Flash an nRF52840 DK with an MCUboot partition layout and `--sysbuild`
   (`SB_CONFIG_BOOTLOADER_MCUBOOT=y`), then run the ztests on the bench.
2. Wire a real BME280 on I2C and validate readings against a reference
   (±0.5 °C, ±3 %RH, ±1 hPa); the `bme280` ztest currently asserts the
   native_sim emulator fixture and needs a bench-specific expected-value mode.
3. Measure real sleep-state current with a Nordic PPK2; the software power
   counters only measure scheduler time, not µA/mW.
4. Confirm MCUboot swap-move on real flash with `tests/ota-sim`.

## Next: production hardening

| Item | Why |
|------|-----|
| MQTT TLS / PSK / username-password | Plaintext MQTT is local-only today. |
| OTA image signature/auth policy | The app stages bytes; MCUboot must validate them. |
| OTA status topic (`status/<device_id>`) | Gives the publisher/operator completion feedback. |
| CBOR schema registry / version gate | Prevents silent decode failures at scale. |
| BLE transport alongside MQTT | Uses the nRF52840 radio; currently unused. |
| Structured logs → Loki/CloudWatch | Operational visibility beyond serial console. |
| Real cloud deployment (Fly.io/AWS IoT) | Moves from local demo to product. |

## Explicit non-goals for now

- BLE, cloud deployment, and TLS are out of scope for the local sandbox.
- The fleet simulator remains synthetic; it is a pipeline load generator, not a
  hardware model.

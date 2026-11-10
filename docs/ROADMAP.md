# Roadmap

The current tree is a working reference platform: firmware builds, all ztests
run, the cloud pipeline is covered by unit + e2e tests, OTA/fault injection are
wired and tested, MQTT uses TLS + password auth, and an nRF52840/MCUboot
cross-build is in CI. Physical hardware validation is the remaining step.

## Current state (v1.0 remediation)

- [x] `native_sim/native/64` default; host toolchain builds without multilib.
- [x] Telemetry: ring buffer, CBOR, QoS1, disconnect-safe draining, sleep-window
  MQTT servicing.
- [x] OTA: documented binary frames, full session/reject ztests, resubscribe,
  flash-corruption hook, host publisher.
- [x] Cloud: topic/device validation, `/health` readiness, golden CBOR fixture,
  main-package tests, new metrics.
- [x] MQTT TLS 1.2+ with CA verification, password auth, and TLS e2e coverage.
- [x] nRF52840 board + MCUboot sysbuild layout, signed-image verification,
  `just build-hil`, and a hardware cross-build CI job.
- [x] Contract/e2e/CI/power-bench/HIL script fixes.
- [x] README stays simple; architecture, data-model, OTA, and audit docs added;
  Excalidraw diagrams.
- [x] MIT license.

## Next: hardware validation

> Exact bill of materials, Canadian sourcing, landed costs, and wiring/compatibility
> notes: see [PROCUREMENT.md](PROCUREMENT.md).

1. Flash the already-cross-built nRF52840 DK image (`just build-hil` output)
   and run the HIL checklist; verify MCUboot swap-move and signature rejection
   with a deliberately invalid image. Add an IP transport (Ethernet/Wi-Fi
   shield or Thread border router) for live MQTT/OTA.
2. Wire a real BME280 on I2C and validate readings against a reference
   (±0.5 °C, ±3 %RH, ±1 hPa); the `bme280` ztest currently asserts the
   native_sim emulator fixture and needs a bench-specific expected-value mode.
3. Measure real sleep-state current with a Nordic PPK2; the software power
   counters only measure scheduler time, not µA/mW.
4. Confirm MCUboot swap-move on real flash with `tests/ota-sim`.

## Next: production hardening

| Item | Why |
|------|-----|
| Production signing key/rotation | MCUboot currently signs with its default debug key; replace it and document rotation. |
| Physical hardware HIL | The nRF52840/MCUboot path is cross-built and signature-verified, not flashed. |
| OTA status topic (`status/<device_id>`) | Gives the publisher/operator completion feedback. |
| CBOR schema registry / version gate | Prevents silent decode failures at scale. |
| BLE transport alongside MQTT | Uses the nRF52840 radio; currently unused. |
| Structured logs → Loki/CloudWatch | Operational visibility beyond serial console. |
| Real cloud deployment (Fly.io/AWS IoT) | Moves from local demo to product. |

## Explicit non-goals for now

- BLE and a hosted cloud deployment are out of scope for the local reference
  stack.
- The fleet simulator remains synthetic; it is a pipeline load generator, not a
  hardware model.
- The committed demo certificates/passwords are not a production PKI; they are
  local-development rotation material.

# tests/ — system-level harnesses

- [`fleet-sim/`](fleet-sim/) — Go MQTT fleet traffic generator; unit, contract,
  and in-process-broker integration tests.
- [`ota-sim/`](ota-sim/) — host-side OTA frame publisher for HIL/bench use.
- [`e2e/`](e2e/) — non-interactive pipeline test (`E2E_MODE=docker|local`).
- [`scripts/`](scripts/) — power-state benchmark and the topic/frame/README
  contract guard.
- [`hil/`](hil/) — optional nRF52840 hardware-in-the-loop flash/test harness.
- Firmware-level ztest suites live under [`../firmware/tests/`](../firmware/tests/);
  the fault-injection *framework tests* are there too, while the fault hooks are
  wired into the application paths.

Run the local quality gates with `just check`; the Docker pipeline with
`just e2e`.

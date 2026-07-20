# tests/ — system-level test harnesses (Phase 4)

Device fleet simulator (Go MQTT traffic generator), fault-injection tests
(dropped packets, sensor/flash faults), power-state benchmarks, and the
optional hardware-in-the-loop (HIL) harness for a real nRF52840 DK. Implemented
in Phase 4. Firmware-level ztest suites live under
[`../firmware/tests/`](../firmware/tests/).

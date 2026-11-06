# Architecture Invariant Audit — zephyr-edge-platform

> **Historical audit (2026-07-20).** Several verdicts predate the OTA/fault
> integration fixes. See [`../AUDIT.md`](../AUDIT.md) and
> [`../ARCHITECTURE.md`](../ARCHITECTURE.md) for the current architecture.

**Audit type:** Final quality gate — architecture invariant verification
**Auditor:** worker-2 (code-reviewer / architecture audit lane)
**Date:** 2026-07-20
**Source of truth:** `.omx/ultragoal/brief.md` → "Architecture Invariants"
**Method:** Static inspection of the monorepo against each invariant. Every
verdict below cites concrete file evidence. No invariant is marked PROVED on
assertion alone.

---

## Summary

| #  | Invariant | Status |
|----|-----------|--------|
| 1  | Monorepo structure (firmware/cloud/infra/tests/docs) | ✅ PROVED |
| 2  | CBOR telemetry schema in `docs/DATA_MODEL.md` with `schema_version` | ✅ PROVED |
| 3  | Zephyr sensor subsystem API for BME280 (not raw I2C) | ✅ PROVED |
| 4  | native_sim as primary CI target (no hardware) | ✅ PROVED |
| 5  | Local-only docker-compose (no external cloud credentials) | ✅ PROVED |
| 6  | MCUboot swap-move OTA with `CONFIG_FLASH_SIMULATOR` on native_sim | ✅ PROVED (with documented design note) |
| 7  | Kconfig-based fault injection flags | ✅ PROVED |
| 8  | Go cloud service: MQTT subscriber → Prometheus exporter | ✅ PROVED |
| 9  | devenv/nix reproducible development environment | ✅ PROVED |
| 10 | All tests on native_sim/QEMU; HIL optional/documented | ✅ PROVED |

**Overall verdict: 10/10 invariants PROVED. Architecture conforms to the brief.**

---

## Detailed Findings

### 1. Monorepo structure — ✅ PROVED

All five required top-level directories exist under `zephyr-edge-platform/`:

- `firmware/` — Zephyr application + custom drivers + ztest suites
- `cloud/` — Go ingestion service (`cloud/main.go`, `cloud/telemetry/`)
- `infra/` — docker-compose observability stack
- `tests/` — fleet-sim, hil, scripts
- `docs/` — `DATA_MODEL.md`, `DEMO.md`

Evidence: top-level `ls` of `zephyr-edge-platform/` shows all five directories
plus supporting `bootloader/`, `justfile`, `devenv.*`, `.github/workflows/`.

### 2. CBOR telemetry schema with `schema_version` — ✅ PROVED

`docs/DATA_MODEL.md` is the single source of truth for the wire format:

- Encoding is **CBOR (RFC 8949)**, enabled via `CONFIG_CBOR`/`CONFIG_ZCBOR`
  (`firmware/prj.conf:38` → `CONFIG_ZCBOR=y`).
- Field table §1.1 defines **key 0 = `schema_version`** (uint, currently `1`),
  marked **Required**.
- §3 "Versioning & Compatibility" explicitly states: *"`schema_version` (key 0)
  is a `uint8`, starting at `1`."*
- Consumer validation rules (§1.4) require rejecting unknown versions and
  incrementing `telemetry_unknown_schema_total`.
- The cloud consumer enforces this: `cloud/main.go` defines
  `unknownSchemaTotal` (`telemetry_unknown_schema_total`) and the decoder lives
  in `cloud/telemetry/telemetry.go`.

### 3. Zephyr sensor subsystem API for BME280 — ✅ PROVED

The BME280 is a first-class Zephyr sensor-subsystem driver, not a raw-I2C
device the application talks to directly:

- `firmware/drivers/sensor/bme280_custom/bme280_custom.c:258` registers
  `static const struct sensor_driver_api bme280_driver_api` with
  `sample_fetch` (`:176`) and `channel_get` (`:237`).
- Channel data is returned through `sensor_value_from_double`
  (`bme280_custom.c:244-250`) for `SENSOR_CHAN_AMBIENT_TEMP/HUMIDITY/PRESS`.
- Devicetree-bound: `DT_DRV_COMPAT vendor_bme280_custom`
  (`bme280_custom.c:17`) with binding
  `firmware/drivers/dts/bindings/sensor/vendor,bme280-custom.yaml`.
- The application consumes it **only** through the sensor API
  (`firmware/src/main.c:39` `DEVICE_DT_GET_ONE(vendor_bme280_custom)`,
  `:49` `sensor_sample_fetch`, `:53-55` `sensor_channel_get`).
- Internally the driver uses the proper Zephyr I2C API
  (`i2c_dt_spec`, `i2c_burst_write_dt`, `i2c_burst_read_dt`,
  `i2c_is_ready_dt`) — i.e. the I2C bus is abstracted by Zephyr, and the app
  never touches raw registers. This satisfies "sensor subsystem API, not raw
  I2C."

### 4. native_sim as primary CI target — ✅ PROVED

- `.github/workflows/ci.yml` sets `env: BOARD: native_sim` globally.
- `firmware-build` runs `west build -b ${{ env.BOARD }} … firmware`.
- `firmware-test` builds and runs all six ztest suites
  (`bme280 sensor_log power_sched telemetry ota fault_inject`) on native_sim.
- No CI job references real hardware or a cross-toolchain; HIL is explicitly
  excluded from CI (see invariant 10).

### 5. Local-only docker-compose — ✅ PROVED

`infra/docker-compose.yml` defines only local services with no external cloud
endpoints:

- `mosquitto` (eclipse-mosquitto:2), `cloud-ingest` (built from `../cloud`),
  `prometheus` (prom/prometheus), `grafana` (grafana/grafana).
- The ingest service points at the **internal** broker
  `MQTT_BROKER: tcp://mosquitto:1883`, topic `telemetry/+`.
- A credential scan of `infra/` and `cloud/` found no AWS/GCP/Azure tokens, no
  external API keys, and no remote endpoints. The only secret-like value is
  `GF_SECURITY_ADMIN_PASSWORD: admin` — a local Grafana dev default, not an
  external cloud credential. Consistent with the brief's "no external cloud
  credentials" constraint.

### 6. MCUboot swap-move OTA with `CONFIG_FLASH_SIMULATOR` — ✅ PROVED

- `firmware/prj.conf:69` → `CONFIG_FLASH_SIMULATOR=y`; `:75`
  `CONFIG_IMG_MANAGER=y`; `:78` `CONFIG_STREAM_FLASH=y`.
- `firmware/src/ota/ota_update.c` writes the image via `flash_img_buffered_write`
  (`:174,:202`) and marks it for swap via `ota_mark_for_swap()` (`:281`) using
  the MCUboot `BOOT_UPGRADE_TEST` swap-move path (`:289`).
- `firmware/tests/ota/boards/native_sim.overlay` defines the MCUboot partition
  layout: `mcuboot` (bootloader area), `slot0` (current image), `slot1` (OTA
  staging), and scratch — the canonical swap-move topology.
- **Design note (not a violation):** `CONFIG_BOOTLOADER_MCUBOOT` is
  intentionally NOT enabled on native_sim (`firmware/prj.conf:72-74`,
  `firmware/tests/ota/prj.conf:6`). MCUboot cannot run as a second-stage
  bootloader on native_sim, so the OTA module drives the `img_manager`/
  `flash_img` swap-move contract directly against the flash simulator. This is
  exactly what the invariant specifies ("MCUboot swap-move … with
  `CONFIG_FLASH_SIMULATOR` on native_sim") and is documented in-code. The
  swap-move semantics (slot1 staging + `BOOT_UPGRADE_TEST` mark + partition
  layout) are faithfully represented.

### 7. Kconfig-based fault injection — ✅ PROVED

- `firmware/Kconfig.appdefs:112-144` defines a Kconfig fault-injection
  framework: `FAULT_INJECT` (master bool, default n) plus per-mode flags
  `FAULT_INJECT_SENSOR_FAIL`, `FAULT_INJECT_FLASH_CORRUPT`,
  `FAULT_INJECT_DROP_PACKETS` (each `depends on FAULT_INJECT`).
- `firmware/prj.conf:86` → `CONFIG_FAULT_INJECT=y`.
- Implementation in `firmware/src/fault_inject.{c,h}`; when disabled the
  `fault_inject_*()` calls compile to no-ops (per Kconfig help text).
- A dedicated ztest suite exists: `firmware/tests/fault_inject/`.

### 8. Go cloud service: MQTT → Prometheus — ✅ PROVED

- `cloud/main.go` imports `paho.mqtt.golang` and
  `prometheus/client_golang` (+ `promhttp`).
- It subscribes to the telemetry topic (`c.Subscribe(mqttTopic, 1, handleMessage)`
  at `cloud/main.go:108`) and exposes Prometheus metrics via
  `mux.Handle("/metrics", promhttp.Handler())` (`:124`).
- Metrics registered (`:26-75`): `telemetry_messages_total`,
  `telemetry_decode_errors_total`, `telemetry_unknown_schema_total`,
  `telemetry_invalid_total`, `telemetry_out_of_range_total`, plus per-device
  temperature/humidity/pressure/seq gauges — matching the DATA_MODEL contract.
- `handleMessage` (`:151`) decodes CBOR, validates, and updates the gauges
  (`:171-174`). Go tests present: `cloud/telemetry/telemetry_test.go`.

### 9. devenv/nix reproducible environment — ✅ PROVED

- `devenv.nix` defines a reproducible dev shell (host Zephyr toolchain: west,
  CMake, Ninja, dtc, gperf, Python deps; git-hooks; `just` orchestrator;
  `ZEPHYR_TOOLCHAIN_VARIANT=host`).
- `devenv.yaml`, `devenv.lock` (pinned inputs), and `.devenv/` state present.
- `.envrc` activates it via `use devenv` (direnv integration).
- Cross-toolchain (zephyr-sdk) is intentionally optional/documented, consistent
  with the native_sim-first approach.

### 10. All tests on native_sim/QEMU; HIL optional/documented — ✅ PROVED

- Six firmware ztest suites under `firmware/tests/`
  (`bme280, sensor_log, power_sched, telemetry, ota, fault_inject`), each with a
  `prj.conf` and (where needed) a `boards/native_sim.overlay`; CI runs all of
  them on native_sim (invariant 4).
- `tests/hil/README.md` explicitly marks HIL as **"optional / best-effort"**,
  states the main verification path is `native_sim`, and confirms **"No CI: HIL
  needs physical hardware, so it is not wired into `.github/workflows/ci.yml`."**
- `tests/fleet-sim/` is a hardware-free Go integration harness
  (`tests/fleet-sim/README.md`: "without needing real hardware or the
  native_sim"), with `contract_test.go`, `telemetry_test.go`, and
  `mqtt_integration_test.go`.
- Therefore every CI-executed test runs on native_sim; the only hardware path
  (HIL) is documented and optional, exactly as the invariant requires.

---

## Residual Observations (non-blocking)

These do not disprove any invariant; flagged for the code-review lane / future
hardening:

- **Grafana default credentials** (`admin/admin`) in `infra/docker-compose.yml`
  are acceptable for a local-only dev stack but should never be reused in any
  non-local deployment.
- **MQTT without TLS** is a documented constraint in the brief (local dev/CI
  limitation); the cloud service defaults to `tcp://` accordingly.
- **OTA on native_sim is a simulation** of the MCUboot swap-move contract
  (img_manager/flash_img + `BOOT_UPGRADE_TEST`), not an end-to-end bootloader
  swap. This is by design and documented; real-hardware swap validation is the
  (optional) HIL path.

## Conclusion

All ten architecture invariants from `.omx/ultragoal/brief.md` are **PROVED**
against the current tree with concrete file-level evidence. The implementation
conforms to the intended architecture. **Recommendation: APPROVE** the
architecture dimension of the final quality gate.

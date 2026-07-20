# zephyr-edge-platform

A production-grade Zephyr RTOS edge device platform: custom BME280 I2C sensor
driver, ring-buffered sensor logging, low-power sleep/wake scheduling, MQTT/CBOR
telemetry, MCUboot OTA, a Go cloud ingestion service, and a local
docker-compose observability stack — all in a reproducible devenv/nix monorepo.

Full design: `.omx/plans/prometheus-strict/zephyr-edge-platform.md`
Telemetry contract: [`docs/DATA_MODEL.md`](docs/DATA_MODEL.md)

## Repository layout

```
zephyr-edge-platform/
├── devenv.nix / devenv.yaml / .envrc   # reproducible dev shell (Zephyr host toolchain)
├── justfile                            # build / test / run orchestration
├── docs/DATA_MODEL.md                  # CBOR telemetry + sensor record schema (shared contract)
├── firmware/                           # Zephyr west workspace (manifest repo)
│   ├── west.yml                        #   west manifest (Zephyr v4.1.0)
│   ├── CMakeLists.txt / Kconfig* / prj.conf
│   ├── boards/native_sim.overlay       #   BME280@0x76 on the native_sim I2C emulator bus
│   ├── src/                            #   main.c, sensor_log.[ch], power_sched.[ch]
│   ├── drivers/                        #   custom BME280 driver as a Zephyr module
│   │   ├── zephyr/module.yml
│   │   ├── dts/bindings/sensor/vendor,bme280-custom.yaml
│   │   └── sensor/bme280_custom/       #     driver + I2C emulator (CONFIG_I2C_EMUL)
│   └── tests/                          #   ztest suites: bme280, sensor_log, power_sched
├── cloud/                              # Go ingestion service (Phase 2)
├── infra/                              # docker-compose + Grafana/Prometheus (Phase 3)
└── tests/                              # fleet sim, fault injection, HIL (Phase 4)
```

## Quick start (Phase 1 — native_sim)

```sh
devenv shell          # enter the reproducible toolchain shell (west, cmake, ninja, dtc, python deps)
just init             # west init -l firmware && west update (first run fetches Zephyr + modules)
just build            # west build -b native_sim firmware
just run              # build + run the firmware (Ctrl-C to stop)
just test             # build + run all ztest suites on native_sim
```

`just run` boots the firmware on the native simulator: the BME280 driver reads
calibration + raw data from the I2C emulator, compensates it, stores timestamped
readings in the ring buffer, and the power scheduler cycles
ACTIVE → IDLE → SLEEP. With the default Kconfig windows (5 s active / 55 s sleep)
you see one sample per minute; shrink `CONFIG_POWER_*_MS` for a faster demo.

## Phase 1 scope (delivered)

1. Monorepo scaffold + devenv/nix shell + top-level justfile
2. `docs/DATA_MODEL.md` CBOR telemetry schema (shared firmware/cloud contract)
3. Custom BME280 I2C sensor driver (Zephyr sensor subsystem API, Bosch
   floating-point compensation) as a standalone Zephyr module
4. BME280 I2C register-file emulator for native_sim (`CONFIG_I2C_EMUL`) with a
   known-answer fixture (25.0 °C / 50.0 %RH / 101.325 kPa)
5. Ring-buffered sensor logging (`sys_ring_buf` + `k_mutex`, overwrite-oldest)
6. Low-power sleep/wake scheduler (Zephyr PM policy locks; software state
   tracking on native_sim — see note below)
7. `main.c` app loop wiring sensor → ring buffer → scheduler
8. ztest suites for bme280 / sensor_log / power_sched on native_sim
9. Kconfig (`SENSOR_LOG_BUF_SIZE`, `POWER_ACTIVE_MS/IDLE/SLEEP`, `DEVICE_ID`),
   `prj.conf`, and `native_sim.overlay`

**Phase 1 gate (verified):** `west build -b native_sim` succeeds; all 15 ztest
cases pass; the driver reads from the I2C emulator; the ring buffer
stores/drains/overwrites correctly; the power scheduler cycles through states.

## Notes & documented limitations

- **Target:** Phase 1 builds and tests on `native_sim`. The nRF52840 (BLE,
  MCUboot OTA, real PM) is the production target for later phases; cross-builds
  need the Zephyr SDK (see the commented `zephyr-sdk` line in `devenv.nix`).
- **Power management on native_sim:** `CONFIG_PM` depends on `HAS_PM`, which
  native_sim does not select, so the PM policy calls in `power_sched.c` compile
  out there (guarded by `#if defined(CONFIG_PM)`). The scheduler still cycles
  ACTIVE/IDLE/SLEEP and tracks per-state time in software; on a PM-capable
  target the policy locks take effect. native_sim measures power-state *time
  distribution*, not real milliwatts.
- **MQTT runs without TLS** in local dev/CI (documented limitation).

## Clean-room credit

Project plan inspired by OMO Prometheus (`code-yeongyu/oh-my-openagent`),
reimplemented from concept under MIT.

# tests/hil — hardware-in-the-loop (HIL) harness

Optional scripts to build, flash, and test the firmware on a **real nRF52840 DK**
instead of `native_sim`. The firmware-level ztest suites live under
[`../../firmware/tests/`](../../firmware/tests/); these scripts drive them on
hardware.

> **Status:** cross-build + signed-image verification pass in CI, but no physical
> DK was available for flash/swap testing. Use this harness to finish the
> hardware validation. The supported automated path remains `native_sim/native/64`
> (`just build` / `just test`). The nRF52840 app uses the real MCUboot sysbuild
> path; `native_sim` still simulates staging because it has no bootloader flash
> offset.

## Prerequisites

1. **nRF52840 DK** connected over USB (provides power + the onboard SEGGER
   J-Link debugger + a serial console).
2. **West workspace** initialized: `just init` from the monorepo root.
3. **Zephyr host toolchain** (devenv shell): `west`, `cmake`, `ninja`, `dtc`.
4. **Zephyr SDK** for Cortex-M4 cross-compilation — `native_sim` builds do *not*
   need it, but `nrf52840dk/nrf52840` does. Set `ZEPHYR_SDK_INSTALL_DIR` to the
   SDK root and use `just build-hil` (or uncomment the `zephyr-sdk` line in
   [`../../devenv.nix`](../../devenv.nix)).
5. A **flash runner**. The DK's onboard J-Link is the default (`west flash`
   picks `jlink`); `pyocd` and `openocd` also work (`--runner pyocd`).
6. *(For automatic test verdicts)* the DK's **serial port**, e.g.
   `/dev/ttyACM0`, readable by your user (on Linux: add yourself to `dialout`).

## Scripts

### `flash.sh` — build + flash one image

```bash
./flash.sh                              # build + flash the app + MCUboot
./flash.sh --sysbuild                   # build + flash app + MCUboot (real swap path)
./flash.sh -a firmware/tests/ota        # flash a specific ztest suite
./flash.sh --runner pyocd               # use pyocd instead of jlink
./flash.sh --skip-build                 # re-flash the existing build dir
./flash.sh --sysbuild --skip-build      # re-flash an existing sysbuild image
./flash.sh --board nrf52840dk/nrf52840  # explicit board (the default)
```

| Option              | Default                  | Meaning                          |
|---------------------|--------------------------|----------------------------------|
| `-b, --board`       | `nrf52840dk/nrf52840`    | target board (env `BOARD`)       |
| `-a, --app`         | `firmware`               | app/suite path (repo-relative)   |
| `-d, --build-dir`   | `build-hil`              | west build directory             |
| `-r, --runner`      | board default (`jlink`)  | `west flash` runner              |
| `-s, --skip-build`  | off                      | flash without rebuilding         |
| `--sysbuild`        | off                      | build/flash firmware + MCUboot   |

### `run_tests.sh` — build + flash + run the ztest suites

```bash
./run_tests.sh                                # flash all suites (manual verify)
./run_tests.sh -t /dev/ttyACM0                # all suites + auto capture/verdict
./run_tests.sh -s telemetry -t /dev/ttyACM0   # one suite, auto verdict
./run_tests.sh --runner pyocd -t /dev/ttyACM0 --timeout 90
```

| Option            | Default                | Meaning                                   |
|-------------------|------------------------|-------------------------------------------|
| `-b, --board`     | `nrf52840dk/nrf52840`  | target board (env `BOARD`)                |
| `-s, --suite`     | all                    | run a single suite                        |
| `-d, --build-dir` | `build-hil`            | base build dir (per-suite subdirs)        |
| `-r, --runner`    | board default          | `west flash` runner                       |
| `-t, --tty`       | none                   | serial device for output capture          |
| `--baud`          | `115200`               | serial baud rate                          |
| `--timeout`       | `60`                   | per-suite capture window (seconds)        |

Suites run: `sensor_log power_sched telemetry fault_inject`.
`bme280` is excluded because it asserts the `native_sim` I2C emulator's exact
fixture values; `ota` is excluded from the automated serial loop but can be
flashed with `--sysbuild` and exercised with `tests/ota-sim`. A real BME280 bench
test and physical MCUboot swap verification are the remaining hardware tasks.

## How verdicts work

After flashing each suite, `run_tests.sh` (with `--tty`) reads the serial
console for `--timeout` seconds and classifies the run:

- **PASS** — log contains `PROJECT EXECUTION SUCCESSFUL` (or per-`TESTSUITE`
  pass markers) and no failure markers.
- **FAIL** — log contains `PROJECT EXECUTION FAILED` (or a `TESTSUITE … failed`
  / `FAIL -` line). The script exits non-zero and prints the log tail.
- **UNKNOWN** — no recognizable marker (test didn't finish in time, wrong baud,
  or no output). Treated as **failure** (the script exits non-zero); inspect the
  captured log under `/tmp/`.

Without `--tty`, suites are built and flashed but reported as `FLASHED`
(verify by watching the console yourself, e.g.
`pyserial-miniterm /dev/ttyACM0 115200`).

## Notes & limitations

- **Board name:** Zephyr v4.1 uses the HWMv2 name `nrf52840dk/nrf52840`. If your
  Zephyr revision still expects the legacy name, pass
  `--board nrf52840dk_nrf52840`.
- **One board at a time:** `run_tests.sh` flashes sequentially and assumes a
  single attached DK. Multi-board HIL needs a per-board runner/serial map.
- **Serial capture is best-effort:** it depends on `pyserial-miniterm`
  (preferred) or an `stty`+`cat` fallback. If the DK enumerates as a different
  port, adjust `--tty`.
- **Real power measurement** (mW) still requires a power monitor / PPK2 in
  series with the DK; these scripts verify functional behavior, not
  milliwatts (consistent with the native_sim power benchmark's documented
  limitation).
- **App image / OTA:** use `./flash.sh --sysbuild` (or `just build-hil`) for the
  MCUboot layout. Flash the MCUboot image and the signed application; `west
  flash -d build-hil --domain firmware` is the usual sysbuild invocation if the
  default flash action complains about multiple domains.
- **Network on the DK:** the nRF52840 DK has no wired IP interface by default.
  The firmware build is cross-compiled and signed, but exercising live MQTT/OTA
  needs a network transport (Ethernet/Wi-Fi shield or a Thread border router
  and a `CONFIG_NET_L2_OPENTHREAD` app configuration).
- **Signing key:** the build uses MCUboot's default debug RSA key and prints a
  warning. Set `SB_CONFIG_BOOT_SIGNATURE_KEY_FILE` to a private key before
  trusting the result; never commit a production key.
- **No physical CI:** the CI `hardware-build` job cross-builds and verifies the
  signature, but flashing/swapping requires the DK. Run this harness locally.

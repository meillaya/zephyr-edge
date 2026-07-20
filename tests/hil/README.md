# tests/hil — hardware-in-the-loop (HIL) harness

Optional scripts to build, flash, and test the firmware on a **real nRF52840 DK**
instead of `native_sim`. The firmware-level ztest suites live under
[`../../firmware/tests/`](../../firmware/tests/); these scripts drive them on
hardware.

> **Status:** optional / best-effort. The main Phase 1–4 verification path is
> `native_sim` (`just build` / `just test`). HIL adds confidence that the same
> code runs on the production Cortex-M4 target with real PM, BLE-capable radio,
> and real flash. It requires physical hardware and a cross-toolchain.

## Prerequisites

1. **nRF52840 DK** connected over USB (provides power + the onboard SEGGER
   J-Link debugger + a serial console).
2. **West workspace** initialized: `just init` from the monorepo root.
3. **Zephyr host toolchain** (devenv shell): `west`, `cmake`, `ninja`, `dtc`.
4. **Zephyr SDK** for Cortex-M4 cross-compilation — `native_sim` builds do *not*
   need it, but `nrf52840dk/nrf52840` does. Uncomment the `zephyr-sdk` line in
   [`../../devenv.nix`](../../devenv.nix) and re-enter the shell.
5. A **flash runner**. The DK's onboard J-Link is the default (`west flash`
   picks `jlink`); `pyocd` and `openocd` also work (`--runner pyocd`).
6. *(For automatic test verdicts)* the DK's **serial port**, e.g.
   `/dev/ttyACM0`, readable by your user (on Linux: add yourself to `dialout`).

## Scripts

### `flash.sh` — build + flash one image

```bash
./flash.sh                              # build + flash the main app
./flash.sh -a firmware/tests/ota        # flash a specific ztest suite
./flash.sh --runner pyocd               # use pyocd instead of jlink
./flash.sh --skip-build                 # re-flash the existing build dir
./flash.sh --board nrf52840dk/nrf52840  # explicit board (the default)
```

| Option              | Default                  | Meaning                          |
|---------------------|--------------------------|----------------------------------|
| `-b, --board`       | `nrf52840dk/nrf52840`    | target board (env `BOARD`)       |
| `-a, --app`         | `firmware`               | app/suite path (repo-relative)   |
| `-d, --build-dir`   | `build-hil`              | west build directory             |
| `-r, --runner`      | board default (`jlink`)  | `west flash` runner              |
| `-s, --skip-build`  | off                      | flash without rebuilding         |

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

Suites run: `bme280 sensor_log power_sched telemetry ota fault_inject`
(the same set as `just test`).

## How verdicts work

After flashing each suite, `run_tests.sh` (with `--tty`) reads the serial
console for `--timeout` seconds and classifies the run:

- **PASS** — log contains `PROJECT EXECUTION SUCCESSFUL` (or per-`TESTSUITE`
  pass markers) and no failure markers.
- **FAIL** — log contains `PROJECT EXECUTION FAILED` (or a `TESTSUITE … failed`
  / `FAIL -` line). The script exits non-zero and prints the log tail.
- **UNKNOWN** — no recognizable marker (test didn't finish in time, wrong baud,
  or no output). Treated as not-pass; inspect the captured log under `/tmp/`.

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
- **No CI:** HIL needs physical hardware, so it is not wired into
  `.github/workflows/ci.yml` (which runs the `native_sim` + cloud + compose
  jobs). Run it locally on a bench DK.

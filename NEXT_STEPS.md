# Next Steps — zephyr-edge-platform Roadmap

Prioritized work items to move from a working native_sim demo to a
hardware-validated, production-ready edge platform.

---

## 1. Wire Dead Subsystems into the Application Loop (Priority: HIGH)

The code review (`docs/CODE_REVIEW.md`, HIGH-2 / MEDIUM-1) identified two
subsystems that compile and pass isolated ztests but are never invoked at
runtime. They are dead code until wired into `firmware/src/main.c`.

### 1.1 OTA Module (`firmware/src/ota/ota_update.c`)

**Current state:** Built into the binary, exposes a complete lifecycle API
(`ota_init` → `ota_subscribe` → `ota_handle_publish` → `ota_process`), and
passes its ztest suite. However, `main.c` never calls any OTA function. The
MQTT command topic (`devices/<device_id>/cmd`, configured via
`CONFIG_OTA_CMD_TOPIC_FMT`) is never subscribed, so incoming OTA commands are
silently ignored.

**Integration required (~50 lines in `main.c`):**

```c
#include "ota/ota_update.h"

/* In main(), after telemetry_init() + telemetry_connect(): */
ota_init();

/* After successful MQTT connect (inside the main loop or a connect hook): */
if (telemetry_is_connected()) {
    ota_subscribe(telemetry_get_client());  /* requires exposing the client */
}

/* In the main loop, alongside telemetry_process(): */
ota_process(telemetry_get_client());
```

**Blocker:** The OTA module needs access to the shared `struct mqtt_client *`
owned by `telemetry.c`. The telemetry header (`telemetry.h`) does not currently
expose a `telemetry_get_client()` accessor. Additionally, the MQTT event
handler in `telemetry.c` must route incoming PUBLISH events on the cmd topic to
`ota_handle_publish()` before processing them as telemetry.

**Acceptance criteria:**
- `ota_init()` called once at startup after `telemetry_init()`.
- `ota_subscribe()` called after each successful MQTT (re)connect.
- Incoming PUBLISH messages on `devices/<device_id>/cmd` are routed to
  `ota_handle_publish()`.
- `ota_process()` called each loop iteration alongside `telemetry_process()`.
- End-to-end test: publish `{"cmd":"ota_start","size":N}` + N bytes of chunks +
  `{"cmd":"ota_finish"}` → `ota_get_state()` returns `OTA_STATE_COMPLETE`.

### 1.2 Fault Injection (`firmware/src/fault_inject.c`)

**Current state:** Kconfig-gated framework (`CONFIG_FAULT_INJECT`,
`CONFIG_FAULT_INJECT_SENSOR_FAIL`, `CONFIG_FAULT_INJECT_FLASH_CORRUPT`,
`CONFIG_FAULT_INJECT_DROP_PACKETS`) with a complete API and passing ztests.
The main application loop never calls `fault_inject_init()` or checks any
fault hooks.

**Integration required (~30 lines in `main.c`):**

```c
#include "fault_inject.h"

/* In main(), before the main loop: */
fault_inject_init();

/* In sample_and_log(), before sensor_sample_fetch(): */
if (fault_inject_sensor_check() != 0) {
    LOG_WRN("sensor fault injected — skipping sample");
    return;
}

/* In drain_and_publish(), before telemetry_publish_reading(): */
if (fault_inject_should_drop_packet()) {
    LOG_DBG("packet fault injected — dropping publish");
    continue;  /* skip this reading */
}
```

**Acceptance criteria:**
- `fault_inject_init()` called once at startup.
- `fault_inject_sensor_check()` gates `sensor_sample_fetch()` in the
  active-window callback.
- `fault_inject_should_drop_packet()` gates MQTT publish in the drain loop.
- With `CONFIG_FAULT_INJECT=y` and a mode enabled at runtime, the affected
  path exhibits the documented failure behavior.
- With `CONFIG_FAULT_INJECT=n` (default), zero overhead (all calls compile
  to no-ops).

---

## 2. Get Real Hardware (Priority: HIGH)

Everything runs on `native_sim`, which is great for CI but means:

| Gap | Why it matters | Hardware needed |
|-----|---------------|-----------------|
| **Power numbers are fictional** | Measuring scheduler time-slices, not actual µA draw. PM subsystem sleep-state entry is unvalidated. | nRF52840 DK ($15) + Nordic PPK2 ($100) |
| **OTA is untested against real flash** | Flash simulator doesn't exercise wear leveling, page erase timing, or actual MCUboot swap semantics. | nRF52840 DK (same board) |
| **I2C is emulated** | Register-file emulator doesn't validate bus timing, pull-up sizing, or actual BME280 compensation math against a physical sensor. | BME280 breakout ($5) |

**Total hardware cost: ~$120** for a meaningful hardware-in-the-loop (HIL) bench.

### First hardware milestone

1. Flash the firmware to an nRF52840 DK (`west build -b nrf52840dk/nrf52840`).
2. Confirm the BME280 emulator still works (devicetree overlay swap).
3. Wire a real BME280 via I2C and validate readings against a known-good
   reference (±0.5 °C, ±3 %RH, ±1 hPa).
4. Measure sleep-state current with the PPK2 — confirm the PM subsystem
   actually enters suspend-to-idle (< 10 µA expected in sleep window).
5. Run an OTA cycle against real flash and confirm MCUboot swap-move succeeds
   on reboot.

---

## 3. Production Hardening (Priority: MEDIUM)

If this moves beyond a demo:

| Gap | Effort | Impact |
|-----|--------|--------|
| MQTT TLS/PSK | Medium | Required for any real deployment — current plaintext MQTT (port 1883) is a documented limitation |
| CBOR schema registry / versioning gate | Low | Prevents silent decode failures at scale when schema_version increments |
| Structured logging → Loki/CloudWatch | Low | Operational visibility beyond serial console |
| Real cloud deployment (Fly.io / AWS IoT) | Medium | Moves from "local demo" to "product" |
| BLE transport alongside MQTT | High | nRF52840's main differentiator, currently unused |

### Additional hardening items from code review

| Item | Source | Effort |
|------|--------|--------|
| Align firmware telemetry topic to `telemetry/%s` (DATA_MODEL.md §4) | CODE_REVIEW HIGH-1 | Low (Kconfig default change + help text fix) |
| Add indefinite-length CBOR test vector to cloud decoder tests | CODE_REVIEW LOW-1 | Low |
| Dedicated 16-bit MQTT packet-id counter (avoid seq wrap collision) | CODE_REVIEW LOW-2 | Low |
| Return non-zero from `main()` on fatal device-init failure | CODE_REVIEW LOW-3 | Trivial |

---

## Honest Assessment

The project is a **solid, well-structured demo/prototype** with good test
coverage for the simulated path. The architecture is sound — Zephyr sensor API,
proper CBOR encoding, clean Go service, reproducible devenv. But it's currently
a **native_sim project that talks about nRF52840** rather than an nRF52840
project.

The single highest-value next step is plugging in a $15 DK board and making the
firmware actually run on real hardware, which would immediately surface the gap
between simulated and real behavior (especially around power management and
flash).

**Recommended execution order:**

1. Wire OTA + fault injection into `main.c` (§1) — unblocks end-to-end testing.
2. Flash to nRF52840 DK and validate on real hardware (§2).
3. Address production hardening items (§3) as deployment requirements solidify.

# Code Review — zephyr-edge-platform (Final Quality Gate)

**Reviewer:** worker-1 (code-reviewer), team `final-quality-gate-fo-00f36ac3`
**Date:** 2026-07-20
**Scope:** `firmware/src/`, `firmware/drivers/sensor/bme280_custom/`, `cloud/`, `tests/fleet-sim/`, `infra/`, plus supporting `firmware/tests/`, `firmware/Kconfig*`, `justfile`, `.github/workflows/ci.yml`.
**Companion artifact:** `docs/ARCHITECTURE_INVARIANT_AUDIT.md` (worker-2, architecture lane).

---

## Verdict: **REQUEST CHANGES**

The code that is present is high quality — clean, well-documented, defensively
written, and well-tested in isolation. However, two subsystems that the platform
claims as core deliverables do **not** function end-to-end in the running
firmware, and one documented contract is internally inconsistent. These are
integration defects, not style issues, and they are disqualifying for a *final*
quality gate even though every test that exists passes.

| Severity | Count |
|----------|-------|
| CRITICAL | 0 |
| HIGH     | 2 |
| MEDIUM   | 2 |
| LOW      | 3 |

---

## Stage 1 — Spec / contract compliance

The ultragoal brief and `docs/DATA_MODEL.md` define the platform contract. Three
contract surfaces were checked: the CBOR telemetry schema, the MQTT topic
conventions, and the OTA/fault-injection deliverables.

- **CBOR schema (DATA_MODEL §1):** ✅ consistent across all three producers/consumers.
  Integer keys 0–8 match exactly between `firmware/src/telemetry/telemetry_cbor.h`,
  `cloud/telemetry/telemetry.go`, and `tests/fleet-sim/main.go`. `schema_version=1`
  is enforced by the cloud decoder and asserted by tests. A dedicated contract test
  (`tests/fleet-sim/contract_test.go:TestCloudDecoderAcceptsFleetSim`) proves the
  cloud decoder accepts fleet-sim payloads.
- **MQTT topic conventions (DATA_MODEL §4):** ❌ **inconsistent** — see HIGH-1.
- **OTA (MCUboot swap-move) deliverable:** ❌ **not integrated** — see HIGH-2.
- **Fault injection (Kconfig flags) deliverable:** ⚠️ framework present and unit
  tested but not wired into any application path — see MEDIUM-1.

---

## Stage 2 — Issues

### [HIGH-1] MQTT topic namespace mismatch: real firmware telemetry is silently dropped by the default cloud config

**Files:**
- `firmware/Kconfig.appdefs:66-72` — `CONFIG_MQTT_TELEMETRY_TOPIC_FMT` default `"devices/%s/telemetry"`
- `cloud/main.go:95` — `mqttTopic := envOr("MQTT_TOPIC", "telemetry/+")`
- `infra/docker-compose.yml:25` — `MQTT_TOPIC: "telemetry/+"`
- `docs/DATA_MODEL.md:16,121` — canonical topic `telemetry/<device_id>`
- `tests/fleet-sim/main.go:385` — publishes `telemetry/<id>`

**Issue.** Three different actors disagree on the telemetry topic namespace:

| Actor | Topic |
|-------|-------|
| Firmware (Kconfig default) | `devices/<device_id>/telemetry` |
| Cloud subscriber (code default + docker-compose) | `telemetry/+` |
| DATA_MODEL.md §4 (canonical contract) | `telemetry/<device_id>` |
| Fleet simulator | `telemetry/<device_id>` |

The cloud service subscribes to `telemetry/+`, which matches the fleet simulator
but **not** the real firmware. A physical/native_sim device publishing with the
default Kconfig topic will log `published seq=…` successfully (QoS1 PUBACK from
the broker) while the cloud ingestion service **never receives a single message**.
This is a silent failure: no error is raised on either side, Prometheus shows no
device metrics, and the Grafana dashboard stays empty for real devices.

The `just demo` quality gate still passes because it drives the pipeline with the
**fleet simulator** (`telemetry/<id>`), not the firmware — so the demo masks the
defect rather than exposing it.

Additionally, the Kconfig help text (`Kconfig.appdefs:71-72`) makes a false claim:
> "The cloud ingestion service subscribes to the matching wildcard (`devices/+/telemetry`)."

The cloud does not subscribe to `devices/+/telemetry`; it subscribes to `telemetry/+`.

**Why HIGH:** the platform's primary purpose — device → cloud telemetry — does not
work by default for real firmware, and the documented contract (DATA_MODEL.md) is
contradicted by the firmware default and by the Kconfig help text.

**Fix (recommended).** `docs/DATA_MODEL.md` is the source of truth and specifies
`telemetry/<device_id>`. Align the firmware to it:
1. Change `CONFIG_MQTT_TELEMETRY_TOPIC_FMT` default from `"devices/%s/telemetry"`
   to `"telemetry/%s"` in `firmware/Kconfig.appdefs`.
2. Correct the help text to state the cloud subscribes to `telemetry/+`.
3. Add an integration assertion (in the demo or a CI smoke step) that a message
   published on the *firmware* topic is observed by the cloud `/metrics`
   (`telemetry_messages_total`), so the two halves can never silently diverge again.

*Alternative:* if `devices/<id>/telemetry` is preferred, change `cloud/main.go`
default and `infra/docker-compose.yml` to `devices/+/telemetry` **and** update
DATA_MODEL.md §4 + the fleet-sim topic. Either way, all four actors must agree, and
a test must lock the agreement.

---

### [HIGH-2] OTA subsystem is compiled into the firmware but never wired into the application (dead code at runtime)

**Files:**
- `firmware/src/main.c` — no reference to OTA anywhere (no `ota_init`, `ota_subscribe`, `ota_process`, `ota_handle_publish`)
- `firmware/src/telemetry/telemetry.c:65-98` — `telemetry_evt_handler` handles only `MQTT_EVT_CONNACK`, `MQTT_EVT_DISCONNECT`, `MQTT_EVT_PUBACK`; **no `MQTT_EVT_PUBLISH` case**, so inbound publishes are never routed to `ota_handle_publish`
- `firmware/src/ota/ota_update.h` — included only by `ota_update.c` itself (verified by grep: no other translation unit includes it)
- `firmware/CMakeLists.txt:22` — `src/ota/ota_update.c` is compiled into `app`

**Issue.** The entire Phase-3 OTA receive path is structurally disconnected from the
running application:
- `main.c` never calls `ota_init()` / `ota_subscribe()`.
- The shared MQTT client's event handler never dispatches `MQTT_EVT_PUBLISH`, so even
  if OTA were initialized there is no code path that could deliver an incoming
  firmware chunk to `ota_handle_publish()`.
- `ota_update.h`'s header comment describes a lifecycle "driven from the application
  main loop" that the application does not implement.

The module compiles and its state API is unit-tested, but as shipped the firmware
**cannot perform an OTA update** — the MCUboot swap-move deliverable (architecture
invariant 6) is non-functional at runtime.

**Why HIGH:** a stated core deliverable / architecture invariant does not work in the
delivered firmware, and the module's own documentation misrepresents its integration
status.

**Fix.** Either integrate it or explicitly descope it:
- *Integrate:* in `main.c`, call `ota_init()` after `telemetry_init()`; after a
  successful `telemetry_connect()`, call `ota_subscribe(&client_ctx)`; add an
  `MQTT_EVT_PUBLISH` case in `telemetry_evt_handler` that calls
  `ota_handle_publish(client, &evt->param.publish)` (and exposes the shared
  `mqtt_client` to the OTA module as its header promises).
- *Descope:* if OTA is intentionally out of scope for this gate, remove
  `ota_update.c` from the app build and document the deferral — do not ship it as
  compiled-but-dead code with documentation claiming it is driven by the main loop.

---

### [MEDIUM-1] Fault-injection framework is not wired into any application path (dead code in app; Kconfig help overstates behavior)

**Files:**
- `firmware/src/main.c` — never calls `fault_inject_init()`
- `firmware/src/telemetry/telemetry.c` — never calls `fault_inject_should_drop_packet()`
- `firmware/src/ota/ota_update.c` — never calls `fault_inject_flash_corrupt_byte()`
- BME280 driver / `main.c` sample path — never calls `fault_inject_sensor_check()`
- `firmware/Kconfig.appdefs:120-143` — help text claims faults corrupt "flash writes
  during OTA" and drop "MQTT publish calls"
- `firmware/CMakeLists.txt:23`, `firmware/prj.conf:86` — `fault_inject.c` compiled in, `CONFIG_FAULT_INJECT=y`

**Issue.** The fault-injection framework (`fault_inject.c`) is correct and its ztest
suite passes, but none of its hooks are invoked from the sensor, flash/OTA, or MQTT
code paths. `fault_inject_init()` is never called by the application, so enabling a
fault mode at runtime has **no observable effect** on the real telemetry/OTA/sensor
pipeline. The Kconfig help text promises behaviors ("flash writes during OTA are
silently corrupted", "MQTT publish calls are silently dropped") that no call site
implements.

**Why MEDIUM (not HIGH):** the framework itself is sound and fully unit-tested; this
is an integration/documentation gap rather than a defect in shipped behavior, and
fault injection is a test aid rather than a product feature. It is aggravated by the
fact that the two paths it claims to affect (OTA flash writes, MQTT publishes) are
themselves not live (HIGH-2).

**Fix.** Wire the hooks into the intended paths (`fault_inject_sensor_check()` before
`sensor_sample_fetch`, `fault_inject_flash_corrupt_byte()` in the OTA flash-write
loop, `fault_inject_should_drop_packet()` before `mqtt_publish`), or document the
framework as a standalone, non-integrated test utility and correct the Kconfig help
text so it does not claim runtime effects that do not exist.

---

### [MEDIUM-2] OTA receive/validate/swap logic has no functional test coverage; test comment claims integration coverage that does not exist

**Files:**
- `firmware/tests/ota/src/main.c` (78 lines) — tests only `ota_init`, `ota_reset`,
  `ota_get_state`, `ota_bytes_received`
- `firmware/tests/ota/src/main.c:73-78` — comment: "Full OTA session tests … are
  exercised at the integration level with a real MQTT broker"

**Issue.** The substantive OTA logic — `ota_start_session` size validation,
`ota_write_chunk` overflow rejection, `ota_finish_session` size-match + swap marking,
and `ota_handle_publish` topic/command dispatch — has **zero** test coverage. The
justifying comment claims integration-level coverage, but no such integration test
exists in the repository (`tests/fleet-sim` is telemetry-only; `tests/hil` is a
hardware flash runner, not an OTA integration test).

This logic is reachable in a unit test: `ota_handle_publish()` and
`ota_mark_for_swap()` are public, and the test `prj.conf` already enables
`CONFIG_FLASH_SIMULATOR`/`IMG_MANAGER`/`STREAM_FLASH`, so a full
`ota_start → chunks → ota_finish` session can be driven with crafted
`mqtt_publish_param` fixtures against the flash simulator.

**Why MEDIUM:** it is a coverage gap (the code reads as correct on inspection: size
bounds, overflow, and mismatch checks are present), but it concerns the most
security-sensitive subsystem and is paired with a misleading coverage claim.

**Fix.** Add ztest cases that drive a full session through `ota_handle_publish`
(success path; oversize `ota_start` rejected; chunk overflow rejected; size-mismatch
on `ota_finish` rejected; `ota_mark_for_swap` reached), and remove or correct the
inaccurate "exercised at the integration level" comment.

---

### [LOW-1] Cloud unit tests only decode definite-length CBOR; the firmware encoder emits indefinite-length maps

**Files:** `cloud/telemetry/telemetry_test.go` (uses `cbor.Marshal`, definite-length);
`firmware/src/telemetry/telemetry_cbor.c:13-14` (zcbor emits indefinite-length maps by
default per the comment).

**Issue.** `fxamacker/cbor` accepts both definite- and indefinite-length maps, so this
is not a bug, but the cloud's decode path is only ever tested against definite-length
payloads while the real firmware produces indefinite-length ones. The indefinite path
is unexercised.

**Fix.** Add one cloud test vector encoded with indefinite-length maps (or a
shared/golden firmware-produced payload) to lock decoder behavior against the actual
wire format.

---

### [LOW-2] QoS1 MQTT `message_id` derived from `seq & 0xFFFF`

**File:** `firmware/src/telemetry/telemetry.c:225-229`

**Issue.** The QoS1 packet id is `(uint16_t)(msg->seq & 0xFFFF)` (forced to 1 if 0).
Because `seq` is monotonic and publishing is single-threaded/low-rate this is safe in
practice, but if `seq` wraps modulo 65536 while a previous PUBACK is still outstanding
the id could be reused. Minor.

**Fix.** Maintain a dedicated rolling 16-bit packet-id counter independent of the
telemetry `seq`, or document why reuse cannot occur.

---

### [LOW-3] `main()` returns 0 (success) when the BME280 is not ready

**File:** `firmware/src/main.c:114-117`

**Issue.** If `device_is_ready(bme280)` is false, the app logs an error and
`return 0;`, signalling success despite a fatal init failure. Harmless on native_sim
(the emulator is always ready) but misleading as a general contract.

**Fix.** Return a non-zero code (or `k_panic()`/loop) on fatal device-init failure so
the failure is observable to a supervisor/CI.

---

## Verification evidence (commands run in this review)

| Check | Result |
|-------|--------|
| `cloud/` `go vet ./...` | clean |
| `cloud/` `go test -count=1 ./...` | `ok …/cloud/telemetry` (15 test funcs) |
| `tests/fleet-sim/` `go vet ./...` | clean |
| `tests/fleet-sim/` `go test -count=1 ./...` | `ok` (5.0s; incl. mini-broker MQTT integration + cloud-decoder contract test) |
| ztest `bme280` (native_sim) | PASS 4/4 |
| ztest `sensor_log` (native_sim) | PASS 5/5 |
| ztest `power_sched` (native_sim) | PASS 5/5 |
| ztest `telemetry` (native_sim) | PASS (CBOR encode suite) |
| ztest `fault_inject` (native_sim) | PASS (all modes) |
| ztest `ota` | **not run** — see gap note below |

**Verification gap (Not-tested):** the `ota` ztest suite could not be built/run in
this shell — the CMake build pins a Nix-store Python that lacks the `west` module
(`ModuleNotFoundError: No module named 'west'`); it requires the devenv shell. A
devenv-based build was attempted but stalled in slow Nix evaluation during this
session. This does not change the OTA findings: the suite source was read directly
and only exercises the state-query API (MEDIUM-2), and the OTA path is not wired into
the app at all (HIGH-2).

**Root-cause / fallback review:** no masking workarounds were found. The telemetry
"best-effort connect" and `native_sim`-without-bridge degradation are explicitly
documented, scoped, and preserve failure evidence (logged `rc`/`LOG_WRN`), so they are
acceptable narrow fallbacks, not failure suppression. HIGH-1/HIGH-2 are genuine
integration gaps, not defensive fallbacks.

---

## What is good (do not regress)

- CBOR schema is single-sourced in DATA_MODEL.md and faithfully mirrored in three
  languages with a cross-language contract test.
- Cloud decoder validation is thorough (schema gate, required-key gate, type gate,
  range flags without rejection) and fully unit-tested incl. boundary values.
- `sensor_log` overwrite-oldest ring buffer is mutex-protected with a dropped counter
  and has concurrency + overflow tests.
- `power_sched` keeps PM `get/put` balanced via an idempotent `set_awake` and is
  runtime-reconfigurable for fast deterministic tests.
- BME280 driver uses the Zephyr sensor API with a register-file I2C emulator and a
  hand-derivable known-answer fixture (25.000 °C / 101.325 kPa / 50.000 %RH).
- Fleet-sim is production-grade Go: deterministic CBOR encoding, per-device RNG
  streams, bounded-publish backpressure, atomic stats, graceful shutdown.
- CI is well-structured (west workspace cache, per-suite ztest, Go race tests, compose
  health gates) and requires no real hardware.

---

## Recommendation

**REQUEST CHANGES.** Resolve HIGH-1 (topic alignment + a test that locks firmware↔cloud
topic agreement) and HIGH-2 (wire OTA into the app or explicitly descope it) before
this can be treated as a passing final quality gate. MEDIUM-1/MEDIUM-2 should be
addressed in the same pass; the LOW items are optional.

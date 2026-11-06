# Data Model — Telemetry & Sensor Records

This document is the **single source of truth** for the telemetry wire format
and the in-device sensor record layout. It is owned by the integrator and is
the shared contract between the firmware (producer) and the cloud ingestion
service (consumer). Both sides MUST validate against this schema.

Encoding: **CBOR** (RFC 8949), enabled in firmware via `CONFIG_CBOR=y`
(Zephyr `<zephyr/data/cbor.h>`). CBOR was chosen over JSON for its compact
binary representation, deterministic encoding, and native Zephyr support.

---

## 1. Telemetry Message (device → cloud)

Published by the device on the MQTT topic `telemetry/<device_id>` (QoS 1).
The payload is a single CBOR map (major type 5). Integer map keys are used
instead of strings to minimize payload size on constrained links.

### 1.1 Field table

| Key | CBOR type | Name             | Unit / format            | Required | Description |
|-----|-----------|------------------|--------------------------|----------|-------------|
| 0   | uint      | `schema_version` | integer (currently `1`)  | yes      | Schema version. Consumer MUST reject unknown versions and increment a `telemetry_unknown_schema_total` counter. |
| 1   | tstr      | `device_id`      | UTF-8 string             | yes      | Device identifier (`CONFIG_DEVICE_ID`, default `native-sim-001`; fleet sim uses `fleet-sim-<N>`). |
| 2   | uint      | `seq`            | integer                  | yes      | Monotonic per-device sequence number (wraps at 2³²). Detects drops/reordering. |
| 3   | uint      | `timestamp_ms`   | integer (ms since boot)  | yes      | `k_uptime_get()` at sample time. Wall-clock alignment is the consumer's job. |
| 4   | float64   | `temperature_c`  | °C (double)              | yes      | Compensated ambient temperature from the BME280. |
| 5   | float64   | `humidity_pct`   | %RH (double, 0–100)      | yes      | Compensated relative humidity from the BME280. |
| 6   | float64   | `pressure_kpa`   | kPa (double)             | yes      | Compensated barometric pressure from the BME280. |
| 7   | uint      | `power_state`    | enum (see §1.2)          | no       | Power state at publish time. |
| 8   | map       | `power_counters` | `{uint => uint}` (ms)    | no       | Cumulative power-state durations: `0`=active_ms, `1`=idle_ms, `2`=sleep_ms. |

### 1.2 `power_state` enum

| Value | Name    | Meaning |
|-------|---------|---------|
| 0     | active  | CPU running, sensor sampling |
| 1     | idle    | runtime-idle / low-power idle window |
| 2     | sleep   | suspend-to-idle sleep window |

### 1.3 CBOR diagnostic example

```
{
  0: 1,
  1: "native-sim-001",
  2: 42,
  3: 210000,
  4: 25.08,
  5: 49.98,
  6: 101.325,
  7: 0,
  8: { 0: 5000, 1: 120, 2: 54880 }
}
```

### 1.4 Validation rules (consumer)

1. Decode CBOR; on parse failure increment `telemetry_decode_errors_total`, drop.
2. If key `0` (schema_version) is missing or `!= 1`, increment
   `telemetry_unknown_schema_total`, drop.
3. Required keys `0,1,2,3,4,5,6` MUST be present; otherwise increment
   `telemetry_invalid_total`, drop. `device_id` (key 1) MUST be a non-empty
   UTF-8 string that is safe as one MQTT topic level.
4. Range-check: `-40 ≤ temperature_c ≤ 85`, `0 ≤ humidity_pct ≤ 100`,
   `30 ≤ pressure_kpa ≤ 115`. Out-of-range values are flagged
   (`telemetry_out_of_range_total`) but still exported.
5. The topic MUST be `telemetry/<device_id>` and its `<device_id>` MUST equal
   key 1. Mismatches are dropped and counted in
   `telemetry_topic_mismatch_total`; this prevents one device from publishing
   under another device's identity.

---

## 2. In-Device Sensor Record (ring buffer)

Stored in the firmware ring buffer (`firmware/src/sensor_log.c`) before
telemetry publication. Fixed-size, naturally aligned struct so records can be
written/read as raw bytes via `sys_ring_buf`.

```c
struct sensor_reading {
    int64_t timestamp_ms;   /* k_uptime_get() at sample time          */
    int32_t temperature_mc; /* temperature in milli-degrees Celsius   */
    int32_t humidity_mpct;  /* relative humidity in milli-percent     */
    int32_t pressure_pa;    /* barometric pressure in pascals         */
};
```

- Size: `sizeof(struct sensor_reading)` — 24 bytes on the default 64-bit
  `native_sim/native/64` target (8-byte `int64_t` alignment), 20 bytes on a
  32-bit target. The code always uses `sizeof`, so record framing is correct
  on any target.
- Ordering: FIFO. Overflow policy: **overwrite oldest** (configurable via
  `CONFIG_SENSOR_LOG_BUF_SIZE`, default 4096 bytes ≈ 170–204 records
  depending on target word size).
- Concurrency: all access serialized by a `k_mutex`.
- Drain API: `sensor_log_get()` pops the oldest record for the telemetry
  publisher; `sensor_log_count()` reports pending records.

### 2.1 Unit conversions (record ↔ telemetry)

| Telemetry field   | Record field        | Conversion |
|-------------------|---------------------|------------|
| `temperature_c`   | `temperature_mc`    | `temperature_mc / 1000.0` |
| `humidity_pct`    | `humidity_mpct`     | `humidity_mpct / 1000.0` |
| `pressure_kpa`    | `pressure_pa`       | `pressure_pa / 1000.0` |

---

## 3. Versioning & Compatibility

- `schema_version` (key 0) is a `uint8`, starting at `1`.
- Additive changes (new optional keys) MAY keep the same version if consumers
  can safely ignore unknown keys (CBOR maps tolerate this).
- Breaking changes (removed/retyped required keys, unit changes) MUST bump the
  version; old and new consumers coexist during rollout via the version gate.

---

## 4. MQTT Topic Conventions

| Topic                     | Direction     | Payload |
|---------------------------|---------------|---------|
| `telemetry/<device_id>`   | device → cloud| CBOR map (§1) |
| `cmd/<device_id>/ota`     | cloud → device| OTA binary frames ([`OTA.md`](OTA.md)) |
| `status/<device_id>`      | device → cloud| reserved for LWT/online status (future) |

The cloud subscriber's `MQTT_TOPIC` must contain exactly one `+` level; the
default is `telemetry/+`. The topic-derived device ID is compared with key 1.

Local dev/CI runs MQTT **without TLS or authentication** (documented
limitation; use only on localhost/trusted networks).

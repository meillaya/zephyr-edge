/*
 * telemetry_cbor.h — CBOR encoding for edge telemetry payloads.
 *
 * Pure encoder for the telemetry map defined in docs/DATA_MODEL.md §1. It has
 * no dependency on the MQTT client or the network stack so it can be unit
 * tested in isolation (firmware/tests/telemetry). The cloud ingestion service
 * (cloud/telemetry) decodes the same map with integer keys 0..8.
 *
 * Map layout (docs/DATA_MODEL.md §1.1):
 *
 *   key 0  schema_version  uint     (== TELEMETRY_SCHEMA_VERSION, currently 1)
 *   key 1  device_id       tstr
 *   key 2  seq             uint     (monotonic per-device sequence number)
 *   key 3  timestamp_ms    uint     (k_uptime_get() at sample time)
 *   key 4  temperature_c   float64  (deg C)
 *   key 5  humidity_pct    float64  (%RH, 0-100)
 *   key 6  pressure_kpa    float64  (kPa)
 *   key 7  power_state     uint     (optional, enum power_state)
 *   key 8  power_counters  map      (optional, {0:active_ms,1:idle_ms,2:sleep_ms})
 */
#ifndef ZEPHYR_EDGE_PLATFORM_TELEMETRY_CBOR_H_
#define ZEPHYR_EDGE_PLATFORM_TELEMETRY_CBOR_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Telemetry schema version (docs/DATA_MODEL.md §3). Embedded as map key 0. */
#define TELEMETRY_SCHEMA_VERSION 1u

/* CBOR integer map keys (docs/DATA_MODEL.md §1.1). Must match the cloud
 * consumer's Key* constants in cloud/telemetry/telemetry.go.
 */
#define TELEMETRY_KEY_SCHEMA_VERSION 0u
#define TELEMETRY_KEY_DEVICE_ID      1u
#define TELEMETRY_KEY_SEQ            2u
#define TELEMETRY_KEY_TIMESTAMP_MS   3u
#define TELEMETRY_KEY_TEMPERATURE_C  4u
#define TELEMETRY_KEY_HUMIDITY_PCT   5u
#define TELEMETRY_KEY_PRESSURE_KPA   6u
#define TELEMETRY_KEY_POWER_STATE    7u
#define TELEMETRY_KEY_POWER_COUNTERS 8u

/* power_counters sub-map keys (docs/DATA_MODEL.md §1.1 key 8). */
#define TELEMETRY_POWER_COUNTER_ACTIVE 0u
#define TELEMETRY_POWER_COUNTER_IDLE   1u
#define TELEMETRY_POWER_COUNTER_SLEEP  2u

/* Maximum number of top-level map keys (7 required + 2 optional power keys). */
#define TELEMETRY_MAP_MAX_KEYS 9u

/*
 * One telemetry message. Field units match docs/DATA_MODEL.md §1.1 and the
 * record->telemetry conversions in §2.1:
 *   temperature_c = temperature_mc / 1000.0
 *   humidity_pct  = humidity_mpct  / 1000.0
 *   pressure_kpa  = pressure_pa    / 1000.0
 *
 * When has_power is false, keys 7 and 8 are omitted from the encoded map (they
 * are optional per the schema).
 */
struct telemetry_msg {
	const char *device_id;     /* key 1: device identifier (CONFIG_DEVICE_ID) */
	uint32_t seq;              /* key 2: monotonic sequence number            */
	uint64_t timestamp_ms;     /* key 3: ms since boot at sample time         */
	double temperature_c;      /* key 4: ambient temperature, deg C           */
	double humidity_pct;       /* key 5: relative humidity, %RH               */
	double pressure_kpa;       /* key 6: barometric pressure, kPa             */
	bool has_power;            /* include keys 7 and 8 when true              */
	uint32_t power_state;      /* key 7: enum power_state at publish time     */
	uint64_t active_ms;        /* key 8[0]: cumulative active-window time     */
	uint64_t idle_ms;          /* key 8[1]: cumulative idle-window time       */
	uint64_t sleep_ms;         /* key 8[2]: cumulative sleep-window time      */
};

/*
 * Encode a telemetry message as a CBOR map into buf.
 *
 * @param msg      Message to encode (device_id must be a NUL-terminated string).
 * @param buf      Destination buffer.
 * @param buf_len  Size of buf in bytes.
 * @param out_len  On success, set to the number of bytes written.
 *
 * @retval 0        Success.
 * @retval -EINVAL  msg, buf, or out_len is NULL, or msg->device_id is NULL.
 * @retval -ENOMEM  buf_len is too small to hold the encoded payload.
 */
int telemetry_cbor_encode(const struct telemetry_msg *msg, uint8_t *buf,
			  size_t buf_len, size_t *out_len);

#endif /* ZEPHYR_EDGE_PLATFORM_TELEMETRY_CBOR_H_ */

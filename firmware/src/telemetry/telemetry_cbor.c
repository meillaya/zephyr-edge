/*
 * telemetry_cbor.c — CBOR encoding for edge telemetry payloads.
 *
 * Implements telemetry_cbor_encode() on top of the zcbor encoder library
 * (CONFIG_ZCBOR). The output is a single CBOR map with integer keys 0..8 as
 * specified in docs/DATA_MODEL.md §1 and decoded by cloud/telemetry.
 *
 * Encoding notes:
 *   - Map keys are CBOR unsigned integers (major type 0) so the cloud consumer
 *     can decode the payload into map[uint64]interface{}.
 *   - temperature_c / humidity_pct / pressure_kpa are encoded as IEEE754
 *     float64 (major type 7) to match the schema's float64 fields.
 *   - zcbor emits indefinite-length maps by default (ZCBOR_CANONICAL off); the
 *     cloud decoder (fxamacker/cbor) accepts both definite and indefinite maps.
 */

#include <errno.h>
#include <string.h>

#include <zcbor_encode.h>

#include "telemetry_cbor.h"

/* Encode one (uint key, uint value) pair. */
static bool put_uint(zcbor_state_t *state, uint32_t key, uint64_t value)
{
	return zcbor_uint32_put(state, key) && zcbor_uint64_put(state, value);
}

/* Encode one (uint key, float64 value) pair. */
static bool put_float(zcbor_state_t *state, uint32_t key, double value)
{
	return zcbor_uint32_put(state, key) && zcbor_float64_put(state, value);
}

int telemetry_cbor_encode(const struct telemetry_msg *msg, uint8_t *buf,
			  size_t buf_len, size_t *out_len)
{
	if (msg == NULL || buf == NULL || out_len == NULL ||
	    msg->device_id == NULL) {
		return -EINVAL;
	}

	/* Two backup levels: one for the top-level map and one for the nested
	 * power_counters map.
	 */
	ZCBOR_STATE_E(state, 2, buf, buf_len, 1);

	bool ok = zcbor_map_start_encode(state, TELEMETRY_MAP_MAX_KEYS);

	/* key 0: schema_version */
	ok = ok && put_uint(state, TELEMETRY_KEY_SCHEMA_VERSION,
			    TELEMETRY_SCHEMA_VERSION);

	/* key 1: device_id (tstr) */
	ok = ok && zcbor_uint32_put(state, TELEMETRY_KEY_DEVICE_ID) &&
	     zcbor_tstr_encode_ptr(state, msg->device_id,
				   strlen(msg->device_id));

	/* key 2: seq */
	ok = ok && put_uint(state, TELEMETRY_KEY_SEQ, msg->seq);

	/* key 3: timestamp_ms */
	ok = ok && put_uint(state, TELEMETRY_KEY_TIMESTAMP_MS,
			    msg->timestamp_ms);

	/* key 4..6: sensor values (float64) */
	ok = ok && put_float(state, TELEMETRY_KEY_TEMPERATURE_C,
			     msg->temperature_c);
	ok = ok && put_float(state, TELEMETRY_KEY_HUMIDITY_PCT,
			     msg->humidity_pct);
	ok = ok && put_float(state, TELEMETRY_KEY_PRESSURE_KPA,
			     msg->pressure_kpa);

	/* key 7..8: optional power state + counters */
	if (ok && msg->has_power) {
		ok = ok && put_uint(state, TELEMETRY_KEY_POWER_STATE,
				    msg->power_state);

		ok = ok && zcbor_uint32_put(state, TELEMETRY_KEY_POWER_COUNTERS) &&
		     zcbor_map_start_encode(state, 3);
		ok = ok && put_uint(state, TELEMETRY_POWER_COUNTER_ACTIVE,
				    msg->active_ms);
		ok = ok && put_uint(state, TELEMETRY_POWER_COUNTER_IDLE,
				    msg->idle_ms);
		ok = ok && put_uint(state, TELEMETRY_POWER_COUNTER_SLEEP,
				    msg->sleep_ms);
		ok = ok && zcbor_map_end_encode(state, 3);
	}

	ok = ok && zcbor_map_end_encode(state, TELEMETRY_MAP_MAX_KEYS);

	if (!ok) {
		return -ENOMEM;
	}

	*out_len = (size_t)(state->payload - buf);
	return 0;
}

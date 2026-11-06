/*
 * main.c — ztest suite for the telemetry CBOR encoder (telemetry_cbor.c).
 *
 * Verifies that telemetry_cbor_encode() produces a CBOR map conforming to
 * docs/DATA_MODEL.md §1 by round-tripping the encoded bytes through the zcbor
 * decoder and asserting every key/value/type. Also covers the schema-version
 * gate, the optional power keys (7,8), the wire-format header, determinism,
 * buffer-too-small, and NULL-argument handling.
 *
 * The decoder used here is an independent CBOR implementation pass over the
 * encoded bytes, so a successful round-trip demonstrates the payload is
 * well-formed RFC 8949 CBOR with the integer-keyed map shape the cloud
 * consumer (cloud/telemetry, fxamacker/cbor) expects.
 */

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include <zcbor_decode.h>

#include "telemetry_cbor.h"

#define TEST_DEVICE_ID "native-sim-001"
#define FLOAT_EPS      1e-9

/* Decoded view of a telemetry payload, populated by decode_telemetry(). */
struct decoded {
	uint64_t schema_version;
	struct zcbor_string device_id;
	uint64_t seq;
	uint64_t timestamp_ms;
	double temperature_c;
	double humidity_pct;
	double pressure_kpa;
	bool has_power_state;
	uint64_t power_state;
	bool has_power_counters;
	uint64_t active_ms;
	uint64_t idle_ms;
	uint64_t sleep_ms;
};

/* Decode a key and assert it equals expected_key. */
static void expect_key(zcbor_state_t *s, uint32_t expected_key)
{
	uint32_t key = UINT32_MAX;

	zassert_true(zcbor_uint32_decode(s, &key), "key decode failed (want %u)",
		     expected_key);
	zassert_equal(key, expected_key, "expected key %u, got %u", expected_key,
		      key);
}

/*
 * Decode the telemetry map in the exact key order the encoder emits
 * (0,1,2,3,4,5,6 and optionally 7,8). Fails the test on any malformed input.
 */
static void decode_telemetry(const uint8_t *buf, size_t len, bool expect_power,
			     struct decoded *out)
{
	ZCBOR_STATE_D(s, 3, buf, len, 1, 0);

	memset(out, 0, sizeof(*out));

	zassert_true(zcbor_map_start_decode(s), "top-level map start failed");

	expect_key(s, TELEMETRY_KEY_SCHEMA_VERSION);
	zassert_true(zcbor_uint64_decode(s, &out->schema_version), "schema_version");

	expect_key(s, TELEMETRY_KEY_DEVICE_ID);
	zassert_true(zcbor_tstr_decode(s, &out->device_id), "device_id");

	expect_key(s, TELEMETRY_KEY_SEQ);
	zassert_true(zcbor_uint64_decode(s, &out->seq), "seq");

	expect_key(s, TELEMETRY_KEY_TIMESTAMP_MS);
	zassert_true(zcbor_uint64_decode(s, &out->timestamp_ms), "timestamp_ms");

	expect_key(s, TELEMETRY_KEY_TEMPERATURE_C);
	zassert_true(zcbor_float64_decode(s, &out->temperature_c), "temperature_c");

	expect_key(s, TELEMETRY_KEY_HUMIDITY_PCT);
	zassert_true(zcbor_float64_decode(s, &out->humidity_pct), "humidity_pct");

	expect_key(s, TELEMETRY_KEY_PRESSURE_KPA);
	zassert_true(zcbor_float64_decode(s, &out->pressure_kpa), "pressure_kpa");

	if (expect_power) {
		expect_key(s, TELEMETRY_KEY_POWER_STATE);
		zassert_true(zcbor_uint64_decode(s, &out->power_state), "power_state");
		out->has_power_state = true;

		expect_key(s, TELEMETRY_KEY_POWER_COUNTERS);
		zassert_true(zcbor_map_start_decode(s), "power_counters map start");
		expect_key(s, TELEMETRY_POWER_COUNTER_ACTIVE);
		zassert_true(zcbor_uint64_decode(s, &out->active_ms), "active_ms");
		expect_key(s, TELEMETRY_POWER_COUNTER_IDLE);
		zassert_true(zcbor_uint64_decode(s, &out->idle_ms), "idle_ms");
		expect_key(s, TELEMETRY_POWER_COUNTER_SLEEP);
		zassert_true(zcbor_uint64_decode(s, &out->sleep_ms), "sleep_ms");
		zassert_true(zcbor_map_end_decode(s), "power_counters map end");
		out->has_power_counters = true;
	}

	zassert_true(zcbor_map_end_decode(s), "top-level map end failed");
}

static struct telemetry_msg sample_msg(bool with_power)
{
	struct telemetry_msg msg = {
		.device_id = TEST_DEVICE_ID,
		.seq = 42,
		.timestamp_ms = 210000,
		.temperature_c = 25.08,
		.humidity_pct = 49.98,
		.pressure_kpa = 101.325,
		.has_power = with_power,
		.power_state = 0, /* active */
		.active_ms = 5000,
		.idle_ms = 120,
		.sleep_ms = 54880,
	};

	return msg;
}

/* ── Tests ─────────────────────────────────────────────────────────── */

ZTEST(telemetry_cbor_tests, test_encode_roundtrip_no_power)
{
	struct telemetry_msg msg = sample_msg(false);
	uint8_t buf[128];
	size_t len = 0;

	zassert_ok(telemetry_cbor_encode(&msg, buf, sizeof(buf), &len));
	zassert_true(len > 0, "encoded length must be > 0");
	zassert_true(len < sizeof(buf), "encoded length %zu within buffer", len);

	struct decoded d;

	decode_telemetry(buf, len, false, &d);

	zassert_equal(d.schema_version, TELEMETRY_SCHEMA_VERSION, "schema_version");
	zassert_equal(d.device_id.len, strlen(TEST_DEVICE_ID), "device_id len");
	zassert_mem_equal(d.device_id.value, TEST_DEVICE_ID, d.device_id.len,
			  "device_id bytes");
	zassert_equal(d.seq, 42, "seq");
	zassert_equal(d.timestamp_ms, 210000, "timestamp_ms");
	zassert_within(d.temperature_c, 25.08, FLOAT_EPS, "temperature_c");
	zassert_within(d.humidity_pct, 49.98, FLOAT_EPS, "humidity_pct");
	zassert_within(d.pressure_kpa, 101.325, FLOAT_EPS, "pressure_kpa");
	zassert_false(d.has_power_state, "power_state must be absent");
	zassert_false(d.has_power_counters, "power_counters must be absent");
}

ZTEST(telemetry_cbor_tests, test_encode_roundtrip_with_power)
{
	struct telemetry_msg msg = sample_msg(true);
	uint8_t buf[160];
	size_t len = 0;

	zassert_ok(telemetry_cbor_encode(&msg, buf, sizeof(buf), &len));

	struct decoded d;

	decode_telemetry(buf, len, true, &d);

	zassert_equal(d.schema_version, TELEMETRY_SCHEMA_VERSION, "schema_version");
	zassert_equal(d.seq, 42, "seq");
	zassert_within(d.temperature_c, 25.08, FLOAT_EPS, "temperature_c");
	zassert_true(d.has_power_state, "power_state present");
	zassert_equal(d.power_state, 0, "power_state value");
	zassert_true(d.has_power_counters, "power_counters present");
	zassert_equal(d.active_ms, 5000, "active_ms");
	zassert_equal(d.idle_ms, 120, "idle_ms");
	zassert_equal(d.sleep_ms, 54880, "sleep_ms");
}

ZTEST(telemetry_cbor_tests, test_schema_version_is_one)
{
	struct telemetry_msg msg = sample_msg(false);
	uint8_t buf[128];
	size_t len = 0;

	zassert_ok(telemetry_cbor_encode(&msg, buf, sizeof(buf), &len));

	struct decoded d;

	decode_telemetry(buf, len, false, &d);
	zassert_equal(d.schema_version, 1u,
		      "schema_version must be 1 (docs/DATA_MODEL.md §3)");
}

ZTEST(telemetry_cbor_tests, test_wire_format_header)
{
	/* The payload must be a CBOR map. zcbor emits an indefinite-length map
	 * (0xBF) by default; the first byte must be a map major type (0xA0-0xBF).
	 */
	struct telemetry_msg msg = sample_msg(false);
	uint8_t buf[128];
	size_t len = 0;

	zassert_ok(telemetry_cbor_encode(&msg, buf, sizeof(buf), &len));
	zassert_true(len >= 2, "payload too short");
	zassert_true((buf[0] & 0xE0) == 0xA0,
		     "first byte 0x%02x is not a CBOR map", buf[0]);

	/* First key is schema_version (key 0 -> CBOR 0x00) and its value is the
	 * uint 1 (CBOR 0x01).
	 */
	zassert_equal(buf[1], 0x00, "first key should be 0 (schema_version)");
	zassert_equal(buf[2], 0x01, "schema_version value should be 1");
}

ZTEST(telemetry_cbor_tests, test_encode_is_deterministic)
{
	struct telemetry_msg msg = sample_msg(true);
	uint8_t buf_a[160];
	uint8_t buf_b[160];
	size_t len_a = 0;
	size_t len_b = 0;

	zassert_ok(telemetry_cbor_encode(&msg, buf_a, sizeof(buf_a), &len_a));
	zassert_ok(telemetry_cbor_encode(&msg, buf_b, sizeof(buf_b), &len_b));

	zassert_equal(len_a, len_b, "length differs across encodings");
	zassert_mem_equal(buf_a, buf_b, len_a, "encoding is not deterministic");
}

ZTEST(telemetry_cbor_tests, test_negative_and_large_values)
{
	/* Exercise the full schema range: negative temperature, boundary
	 * humidity/pressure, large seq and timestamp.
	 */
	struct telemetry_msg msg = {
		.device_id = "fleet-sim-49",
		.seq = 0xFFFFFFFFu,
		.timestamp_ms = 0x1122334455667788ull,
		.temperature_c = -40.0,
		.humidity_pct = 100.0,
		.pressure_kpa = 30.0,
		.has_power = false,
	};
	uint8_t buf[128];
	size_t len = 0;

	zassert_ok(telemetry_cbor_encode(&msg, buf, sizeof(buf), &len));

	struct decoded d;

	decode_telemetry(buf, len, false, &d);
	zassert_equal(d.seq, 0xFFFFFFFFu, "seq wrap");
	zassert_equal(d.timestamp_ms, 0x1122334455667788ull, "timestamp 64-bit");
	zassert_within(d.temperature_c, -40.0, FLOAT_EPS, "negative temp");
	zassert_within(d.humidity_pct, 100.0, FLOAT_EPS, "humidity bound");
	zassert_within(d.pressure_kpa, 30.0, FLOAT_EPS, "pressure bound");
	zassert_equal(d.device_id.len, strlen("fleet-sim-49"), "device_id len");
	zassert_mem_equal(d.device_id.value, "fleet-sim-49", d.device_id.len,
			  "device_id bytes");
}

ZTEST(telemetry_cbor_tests, test_buffer_too_small)
{
	struct telemetry_msg msg = sample_msg(true);
	uint8_t buf[4]; /* far too small for the payload */
	size_t len = 0;

	zassert_equal(telemetry_cbor_encode(&msg, buf, sizeof(buf), &len), -ENOMEM,
		      "expected -ENOMEM for tiny buffer");
}

ZTEST(telemetry_cbor_tests, test_null_arguments)
{
	struct telemetry_msg msg = sample_msg(false);
	uint8_t buf[128];
	size_t len = 0;

	zassert_equal(telemetry_cbor_encode(NULL, buf, sizeof(buf), &len), -EINVAL,
		      "NULL msg");
	zassert_equal(telemetry_cbor_encode(&msg, NULL, sizeof(buf), &len), -EINVAL,
		      "NULL buf");
	zassert_equal(telemetry_cbor_encode(&msg, buf, sizeof(buf), NULL), -EINVAL,
		      "NULL out_len");

	struct telemetry_msg no_id = sample_msg(false);

	no_id.device_id = NULL;
	zassert_equal(telemetry_cbor_encode(&no_id, buf, sizeof(buf), &len), -EINVAL,
		      "NULL device_id");
}

/*
 * Byte-exact golden payload shared with cloud/telemetry/testdata/firmware-v1.cbor.
 * It locks the wire format (including zcbor's indefinite-length maps and
 * float64 encoding) so the firmware and the Go consumer cannot drift silently.
 */
static const uint8_t golden_payload[] = {
	0xbf, 0x00, 0x01, 0x01, 0x6e, 0x6e, 0x61, 0x74, 0x69, 0x76,
	0x65, 0x2d, 0x73, 0x69, 0x6d, 0x2d, 0x30, 0x30, 0x31, 0x02,
	0x18, 0x2a, 0x03, 0x1a, 0x00, 0x03, 0x34, 0x50, 0x04, 0xfb,
	0x40, 0x39, 0x14, 0x7a, 0xe1, 0x47, 0xae, 0x14, 0x05, 0xfb,
	0x40, 0x48, 0xfd, 0x70, 0xa3, 0xd7, 0x0a, 0x3d, 0x06, 0xfb,
	0x40, 0x59, 0x54, 0xcc, 0xcc, 0xcc, 0xcc, 0xcd, 0x07, 0x00,
	0x08, 0xbf, 0x00, 0x19, 0x13, 0x88, 0x01, 0x18, 0x78, 0x02,
	0x19, 0xd6, 0x60, 0xff, 0xff,
};

ZTEST(telemetry_cbor_tests, test_golden_wire_bytes)
{
	struct telemetry_msg msg = sample_msg(true);
	uint8_t buf[160];
	size_t len = 0;

	zassert_ok(telemetry_cbor_encode(&msg, buf, sizeof(buf), &len));
	zassert_equal(len, sizeof(golden_payload), "payload length %zu", len);
	zassert_mem_equal(buf, golden_payload, sizeof(golden_payload),
			  "wire bytes differ from the shared golden fixture");
}

ZTEST_SUITE(telemetry_cbor_tests, NULL, NULL, NULL, NULL, NULL);

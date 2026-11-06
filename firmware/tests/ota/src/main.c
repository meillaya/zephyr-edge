/*
 * main.c — ztest suite for the OTA update module (ota_update.c).
 *
 * Drives the full frame protocol (start/data/finish/abort) against the
 * native_sim flash simulator and verifies every rejection path: malformed
 * control frames, zero/oversized images, data before start, chunk overflow,
 * and finish-size mismatch. This is the session-level coverage that the old
 * state-getter-only suite claimed but did not provide.
 */

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/net/mqtt.h>
#include <zephyr/ztest.h>

#include "ota_update.h"

#define TEST_TOPIC "cmd/native-sim-001/ota"

static void ota_before(void *fixture)
{
	ARG_UNUSED(fixture);
	ota_reset();
	zassert_ok(ota_init(), "ota_init failed");
}

ZTEST_SUITE(ota_tests, NULL, NULL, ota_before, NULL, NULL);

static void make_start(uint8_t frame[OTA_START_FRAME_LEN], uint32_t size)
{
	frame[0] = OTA_FRAME_START;
	frame[1] = (uint8_t)(size >> 24);
	frame[2] = (uint8_t)(size >> 16);
	frame[3] = (uint8_t)(size >> 8);
	frame[4] = (uint8_t)size;
}

ZTEST(ota_tests, test_init_succeeds)
{
	zassert_equal(ota_get_state(), OTA_STATE_IDLE, "state not IDLE after init");
	zassert_equal(ota_bytes_received(), 0, "bytes_received not 0 after init");
	zassert_false(ota_is_subscribed(), "must not be subscribed before CONNACK");
}

ZTEST(ota_tests, test_topic_match)
{
	struct mqtt_topic match = {
		.topic = {
			.utf8 = (uint8_t *)TEST_TOPIC,
			.size = strlen(TEST_TOPIC),
		},
		.qos = MQTT_QOS_1_AT_LEAST_ONCE,
	};
	struct mqtt_topic other = {
		.topic = {
			.utf8 = (uint8_t *)"cmd/other/ota",
			.size = strlen("cmd/other/ota"),
		},
		.qos = MQTT_QOS_1_AT_LEAST_ONCE,
	};

	zassert_true(ota_matches_topic(&match), "topic should match");
	zassert_false(ota_matches_topic(&other), "topic should not match");
	zassert_false(ota_matches_topic(NULL), "NULL topic should not match");
}

ZTEST(ota_tests, test_full_session)
{
	uint8_t start[OTA_START_FRAME_LEN];
	uint8_t data_a[] = {OTA_FRAME_DATA, 'A', 'B'};
	uint8_t data_b[] = {OTA_FRAME_DATA, 'C', 'D'};
	uint8_t finish[] = {OTA_FRAME_FINISH};

	make_start(start, 4);

	zassert_true(ota_handle_frame(start, sizeof(start)));
	zassert_equal(ota_get_state(), OTA_STATE_RECEIVING);

	zassert_true(ota_handle_frame(data_a, sizeof(data_a)));
	zassert_equal(ota_bytes_received(), 2);
	zassert_true(ota_handle_frame(data_b, sizeof(data_b)));
	zassert_equal(ota_bytes_received(), 4);

	zassert_true(ota_handle_frame(finish, sizeof(finish)));
	zassert_equal(ota_get_state(), OTA_STATE_COMPLETE);
	zassert_equal(ota_bytes_received(), 4);
}

ZTEST(ota_tests, test_reject_zero_size)
{
	uint8_t start[OTA_START_FRAME_LEN];

	make_start(start, 0);
	zassert_true(ota_handle_frame(start, sizeof(start)));
	zassert_equal(ota_get_state(), OTA_STATE_ERROR);
}

ZTEST(ota_tests, test_reject_oversized_image)
{
	uint8_t start[OTA_START_FRAME_LEN];

	make_start(start, CONFIG_OTA_MAX_IMAGE_SIZE + 1u);
	zassert_true(ota_handle_frame(start, sizeof(start)));
	zassert_equal(ota_get_state(), OTA_STATE_ERROR);
}

ZTEST(ota_tests, test_reject_malformed_start)
{
	uint8_t short_start[OTA_START_FRAME_LEN - 1] = {OTA_FRAME_START, 0, 0, 1};

	zassert_true(ota_handle_frame(short_start, sizeof(short_start)));
	zassert_equal(ota_get_state(), OTA_STATE_ERROR);
}

ZTEST(ota_tests, test_data_before_start_ignored)
{
	uint8_t data[] = {OTA_FRAME_DATA, 'X'};

	zassert_true(ota_handle_frame(data, sizeof(data)));
	zassert_equal(ota_get_state(), OTA_STATE_IDLE);
	zassert_equal(ota_bytes_received(), 0);
}

ZTEST(ota_tests, test_reject_chunk_overflow)
{
	uint8_t start[OTA_START_FRAME_LEN];
	uint8_t data[] = {OTA_FRAME_DATA, 'A', 'B', 'C'};

	make_start(start, 2);
	zassert_true(ota_handle_frame(start, sizeof(start)));
	zassert_true(ota_handle_frame(data, sizeof(data)));
	zassert_equal(ota_get_state(), OTA_STATE_ERROR);
}

ZTEST(ota_tests, test_reject_finish_size_mismatch)
{
	uint8_t start[OTA_START_FRAME_LEN];
	uint8_t data[] = {OTA_FRAME_DATA, 'A', 'B'};
	uint8_t finish[] = {OTA_FRAME_FINISH};

	make_start(start, 4);
	zassert_true(ota_handle_frame(start, sizeof(start)));
	zassert_true(ota_handle_frame(data, sizeof(data)));
	zassert_true(ota_handle_frame(finish, sizeof(finish)));
	zassert_equal(ota_get_state(), OTA_STATE_ERROR);
}

ZTEST(ota_tests, test_abort_resets)
{
	uint8_t start[OTA_START_FRAME_LEN];
	uint8_t abort[] = {OTA_FRAME_ABORT};

	make_start(start, 8);
	zassert_true(ota_handle_frame(start, sizeof(start)));
	zassert_equal(ota_get_state(), OTA_STATE_RECEIVING);

	zassert_true(ota_handle_frame(abort, sizeof(abort)));
	zassert_equal(ota_get_state(), OTA_STATE_IDLE);
	zassert_equal(ota_bytes_received(), 0);
}

ZTEST(ota_tests, test_unknown_opcode_is_consumed)
{
	uint8_t frame[] = {0x7F, 0x00};

	zassert_true(ota_handle_frame(frame, sizeof(frame)));
	zassert_equal(ota_get_state(), OTA_STATE_IDLE);
}

ZTEST(ota_tests, test_mark_for_swap)
{
	zassert_ok(ota_mark_for_swap());
}

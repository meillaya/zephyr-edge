/*
 * ota_update.c — MQTT-triggered OTA firmware update (MCUboot + flash simulator).
 *
 * Implements the receive path described in ota_update.h. Image frames arrive
 * from telemetry.c (which reads and acknowledges the MQTT PUBLISH payload),
 * are written to the OTA slot with stream_flash, and are marked for MCUboot
 * swap-move on completion.
 *
 * The implementation is intentionally simple and CI-friendly:
 *   - Single-threaded (driven from telemetry's publish callback).
 *   - No TLS / authentication (documented limitation, matches prj.conf).
 *   - Flash simulator on native_sim; real hardware uses the same code path
 *     with the actual flash driver.
 *
 * On native_sim, CONFIG_BOOTLOADER_MCUBOOT is not enabled because it requires
 * HAS_FLASH_LOAD_OFFSET. The OTA module drives img_manager/flash_img directly
 * and boot_request_upgrade() is compiled only when MCUboot is available.
 */

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/mqtt.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/dfu/flash_img.h>

#if defined(CONFIG_BOOTLOADER_MCUBOOT)
#include <zephyr/dfu/mcuboot.h>
#endif

#include "fault_inject.h"
#include "ota_update.h"

LOG_MODULE_REGISTER(OTA, CONFIG_LOG_DEFAULT_LEVEL);

#define OTA_TOPIC_BUF_SIZE 64
#define OTA_SUB_MESSAGE_ID 0x07A1

/* The telemetry RX scratch buffer must be large enough for one data frame. */
BUILD_ASSERT(CONFIG_TELEMETRY_RX_PAYLOAD_MAX >= CONFIG_OTA_CHUNK_SIZE + 1,
	     "CONFIG_TELEMETRY_RX_PAYLOAD_MAX must cover OTA_CHUNK_SIZE + 1");

#if DT_HAS_FIXED_PARTITION_LABEL(slot1_partition)
BUILD_ASSERT(CONFIG_OTA_MAX_IMAGE_SIZE <= FIXED_PARTITION_SIZE(slot1_partition),
	     "CONFIG_OTA_MAX_IMAGE_SIZE exceeds the slot1 partition");
#endif

static enum ota_state state = OTA_STATE_IDLE;
static size_t expected_size;
static size_t bytes_received;
static char topic_buf[OTA_TOPIC_BUF_SIZE];
static bool subscribed;
static bool ready;
static struct flash_img_context flash_ctx;

/* Per-frame scratch buffer; corrupting it exercises flash degradation without
 * mutating the caller's const payload buffer.
 */
static uint8_t chunk_buf[CONFIG_OTA_CHUNK_SIZE];

/* Subscription topic storage for mqtt_subscription_list. */
static struct mqtt_topic sub_topic;
static struct mqtt_subscription_list sub_list;

int ota_init(void)
{
	state = OTA_STATE_IDLE;
	expected_size = 0;
	bytes_received = 0;
	subscribed = false;
	ready = false;

	int n = snprintf(topic_buf, sizeof(topic_buf), CONFIG_OTA_CMD_TOPIC_FMT,
			 CONFIG_DEVICE_ID);
	if (n < 0 || (size_t)n >= sizeof(topic_buf)) {
		LOG_ERR("OTA topic too long");
		return -ENOMEM;
	}

	ready = true;
	LOG_INF("OTA init: cmd topic=\"%s\" max_image=%d chunk=%d", topic_buf,
		CONFIG_OTA_MAX_IMAGE_SIZE, CONFIG_OTA_CHUNK_SIZE);
	return 0;
}

bool ota_matches_topic(const struct mqtt_topic *topic)
{
	if (!ready || topic == NULL || topic->topic.utf8 == NULL) {
		return false;
	}

	size_t len = strlen(topic_buf);

	return topic->topic.size == len &&
	       memcmp(topic->topic.utf8, topic_buf, len) == 0;
}

int ota_subscribe(struct mqtt_client *client)
{
	if (!ready) {
		return -EINVAL;
	}
	if (subscribed) {
		return 0;
	}
	if (client == NULL) {
		return -EINVAL;
	}

	sub_topic.topic.utf8 = (uint8_t *)topic_buf;
	sub_topic.topic.size = strlen(topic_buf);
	sub_topic.qos = MQTT_QOS_1_AT_LEAST_ONCE;

	sub_list.list = &sub_topic;
	sub_list.list_count = 1;
	sub_list.message_id = OTA_SUB_MESSAGE_ID;

	int err = mqtt_subscribe(client, &sub_list);
	if (err != 0) {
		LOG_WRN("OTA subscribe failed: %d", err);
		return err;
	}

	subscribed = true;
	LOG_INF("OTA subscribed to %s", topic_buf);
	return 0;
}

void ota_on_connected(struct mqtt_client *client)
{
	if (!ready) {
		return;
	}
	if (state == OTA_STATE_RECEIVING) {
		LOG_WRN("reconnect during OTA; abandoning partial image");
		state = OTA_STATE_ERROR;
	}

	/* A new connection has no subscriptions, regardless of the previous
	 * session state. */
	subscribed = false;
	(void)ota_subscribe(client);
}

void ota_on_disconnected(void)
{
	subscribed = false;
	if (state == OTA_STATE_RECEIVING) {
		LOG_WRN("disconnect during OTA; abandoning partial image");
		state = OTA_STATE_ERROR;
	}
}

static int ota_start_session(size_t size)
{
	if (size == 0 || size > CONFIG_OTA_MAX_IMAGE_SIZE) {
		LOG_ERR("OTA rejected: size=%zu (max=%d)", size,
			CONFIG_OTA_MAX_IMAGE_SIZE);
		state = OTA_STATE_ERROR;
		return -EINVAL;
	}

#if DT_HAS_FIXED_PARTITION_LABEL(slot1_partition)
	if (size > FIXED_PARTITION_SIZE(slot1_partition)) {
		LOG_ERR("OTA rejected: size=%zu exceeds slot1 (%u)", size,
			FIXED_PARTITION_SIZE(slot1_partition));
		state = OTA_STATE_ERROR;
		return -EINVAL;
	}
#endif

	int err = flash_img_init(&flash_ctx);
	if (err != 0) {
		LOG_ERR("flash_img_init failed: %d", err);
		state = OTA_STATE_ERROR;
		return err;
	}

	expected_size = size;
	bytes_received = 0;
	state = OTA_STATE_RECEIVING;
	LOG_INF("OTA start: expecting %zu bytes", size);
	return 0;
}

static int ota_write_chunk(const uint8_t *data, size_t len)
{
	if (state != OTA_STATE_RECEIVING) {
		return -EAGAIN;
	}
	if (data == NULL || len == 0 || len > sizeof(chunk_buf)) {
		LOG_ERR("OTA invalid chunk length: %zu", len);
		state = OTA_STATE_ERROR;
		return -EINVAL;
	}

	if (bytes_received + len > expected_size) {
		LOG_ERR("OTA overflow: %zu + %zu > %zu", bytes_received, len,
			expected_size);
		state = OTA_STATE_ERROR;
		return -ENOSPC;
	}

	for (size_t i = 0; i < len; i++) {
		chunk_buf[i] = fault_inject_flash_corrupt_byte(data[i]);
	}

	/* flush=false for intermediate chunks; the final chunk is flushed in
	 * ota_finish_session().
	 */
	bool flush = (bytes_received + len >= expected_size);
	int err = flash_img_buffered_write(&flash_ctx, chunk_buf, len, flush);

	if (err != 0) {
		LOG_ERR("flash_img_buffered_write failed: %d", err);
		state = OTA_STATE_ERROR;
		return err;
	}

	bytes_received += len;
	LOG_DBG("OTA chunk: %zu bytes (total %zu/%zu)", len, bytes_received,
		expected_size);
	return 0;
}

static int ota_finish_session(void)
{
	if (state != OTA_STATE_RECEIVING) {
		return -EAGAIN;
	}

	if (bytes_received != expected_size) {
		LOG_ERR("OTA size mismatch: got %zu, expected %zu",
			bytes_received, expected_size);
		state = OTA_STATE_ERROR;
		return -EINVAL;
	}

	/* Flush any remaining buffered data. */
	int err = flash_img_buffered_write(&flash_ctx, chunk_buf, 0, true);

	if (err != 0) {
		LOG_ERR("flash flush failed: %d", err);
		state = OTA_STATE_ERROR;
		return err;
	}

	err = ota_mark_for_swap();
	if (err != 0) {
		state = OTA_STATE_ERROR;
		return err;
	}

	state = OTA_STATE_COMPLETE;
	LOG_INF("OTA complete: %zu bytes written, image marked for swap",
		bytes_received);
	return 0;
}

bool ota_handle_frame(const uint8_t *frame, size_t len)
{
	if (!ready || frame == NULL || len == 0) {
		return false;
	}

	switch (frame[0]) {
	case OTA_FRAME_START: {
		if (len != OTA_START_FRAME_LEN) {
			LOG_ERR("OTA malformed start frame (%zu bytes)", len);
			state = OTA_STATE_ERROR;
			return true;
		}
		uint32_t size = ((uint32_t)frame[1] << 24) |
				((uint32_t)frame[2] << 16) |
				((uint32_t)frame[3] << 8) |
				(uint32_t)frame[4];

		(void)ota_start_session(size);
		return true;
	}

	case OTA_FRAME_DATA:
		(void)ota_write_chunk(frame + 1, len - 1);
		return true;

	case OTA_FRAME_FINISH:
		if (len != OTA_CONTROL_FRAME_LEN) {
			LOG_ERR("OTA malformed finish frame (%zu bytes)", len);
			state = OTA_STATE_ERROR;
			return true;
		}
		(void)ota_finish_session();
		return true;

	case OTA_FRAME_ABORT:
		if (len != OTA_CONTROL_FRAME_LEN) {
			LOG_ERR("OTA malformed abort frame (%zu bytes)", len);
			state = OTA_STATE_ERROR;
			return true;
		}
		ota_reset();
		LOG_INF("OTA aborted");
		return true;

	default:
		LOG_WRN("OTA unknown frame opcode 0x%02x", frame[0]);
		if (state == OTA_STATE_RECEIVING) {
			state = OTA_STATE_ERROR;
		}
		return true;
	}
}

enum ota_state ota_get_state(void)
{
	return state;
}

size_t ota_bytes_received(void)
{
	return bytes_received;
}

bool ota_is_subscribed(void)
{
	return subscribed;
}

int ota_mark_for_swap(void)
{
#if defined(CONFIG_BOOTLOADER_MCUBOOT)
	int err = boot_request_upgrade(BOOT_UPGRADE_TEST);

	if (err != 0) {
		LOG_ERR("boot_request_upgrade failed: %d", err);
		return err;
	}
	LOG_INF("image marked for MCUboot swap (BOOT_UPGRADE_TEST)");
	return 0;
#else
	/* On native_sim without CONFIG_BOOTLOADER_MCUBOOT, the image is staged
	 * in slot1; a real bootloader would validate and swap it on reboot.
	 */
	LOG_INF("OTA image staged (native_sim simulation; no MCUboot swap)");
	return 0;
#endif
}

void ota_reset(void)
{
	state = OTA_STATE_IDLE;
	expected_size = 0;
	bytes_received = 0;
	LOG_DBG("OTA state reset");
}

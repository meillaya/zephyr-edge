/*
 * ota_update.c — MQTT-triggered OTA firmware update (MCUboot + flash simulator).
 *
 * Implements the OTA receive path described in ota_update.h. Firmware image
 * chunks arrive as MQTT PUBLISH payloads on the cmd topic; they are written to
 * the flash simulator via stream_flash, then marked for MCUboot swap-move on
 * completion.
 *
 * The implementation is intentionally simple and CI-friendly:
 *   - Single-threaded (driven from the application loop via ota_process).
 *   - No TLS / authentication (documented limitation, matches prj.conf).
 *   - Flash simulator on native_sim; real hardware uses the same code path
 *     with the actual flash driver.
 *
 * Note: On native_sim, CONFIG_BOOTLOADER_MCUBOOT is not enabled because it
 * requires HAS_FLASH_LOAD_OFFSET. The OTA module uses img_manager/flash_img
 * APIs for simulation; the boot_request_upgrade() call is conditionally
 * compiled only when CONFIG_BOOTLOADER_MCUBOOT is available.
 */

#include <stdio.h>
#include <string.h>
#include <errno.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/mqtt.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/dfu/flash_img.h>

#if defined(CONFIG_BOOTLOADER_MCUBOOT)
#include <zephyr/dfu/mcuboot.h>
#endif

#include "ota_update.h"

LOG_MODULE_REGISTER(OTA, CONFIG_LOG_DEFAULT_LEVEL);

/* Flash area for the OTA image slot (MCUboot slot1_partition). */
#if DT_HAS_FIXED_PARTITION_LABEL(slot1_partition)
#define OTA_FLASH_AREA_ID FIXED_PARTITION_ID(slot1_partition)
#else
/* Fallback for native_sim without explicit slot1_partition. */
#define OTA_FLASH_AREA_ID 1
#endif

/* Stream flash write buffer. */
#define OTA_WRITE_BUF_SIZE CONFIG_OTA_CHUNK_SIZE

/* Command topic buffer. */
#define OTA_TOPIC_BUF_SIZE 64

/* Simple command detection: look for these substrings in the payload. */
#define OTA_CMD_START  "ota_start"
#define OTA_CMD_FINISH "ota_finish"

static enum ota_state state = OTA_STATE_IDLE;
static size_t expected_size;
static size_t bytes_received;
static char topic_buf[OTA_TOPIC_BUF_SIZE];
static bool subscribed;

/* Flash image context (MCUboot dfu). */
static struct flash_img_context flash_ctx;
static bool flash_ctx_initialized;

/* Subscription topic storage for mqtt_subscription_list. */
static struct mqtt_topic sub_topic;
static struct mqtt_subscription_list sub_list;

int ota_init(void)
{
	state = OTA_STATE_IDLE;
	expected_size = 0;
	bytes_received = 0;
	subscribed = false;
	flash_ctx_initialized = false;

	int n = snprintf(topic_buf, sizeof(topic_buf), CONFIG_OTA_CMD_TOPIC_FMT,
			 CONFIG_DEVICE_ID);
	if (n < 0 || (size_t)n >= sizeof(topic_buf)) {
		LOG_ERR("OTA topic too long");
		return -ENOMEM;
	}

	LOG_INF("OTA init: cmd topic=\"%s\" max_image=%d", topic_buf,
		CONFIG_OTA_MAX_IMAGE_SIZE);
	return 0;
}

int ota_subscribe(struct mqtt_client *client)
{
	if (subscribed) {
		return 0;
	}

	sub_topic.topic.utf8 = (uint8_t *)topic_buf;
	sub_topic.topic.size = strlen(topic_buf);
	sub_topic.qos = MQTT_QOS_1_AT_LEAST_ONCE;

	sub_list.list = &sub_topic;
	sub_list.list_count = 1;
	sub_list.message_id = 0x07A1; /* non-zero subscription id */

	int err = mqtt_subscribe(client, &sub_list);
	if (err != 0) {
		LOG_WRN("OTA subscribe failed: %d", err);
		return err;
	}

	subscribed = true;
	LOG_INF("OTA subscribed to %s", topic_buf);
	return 0;
}

/* Parse the expected image size from an ota_start payload.
 * Looks for "size":<N> in the payload string.
 */
static size_t parse_start_size(const uint8_t *data, size_t len)
{
	const char *key = "\"size\":";
	const char *p = memmem(data, len, key, strlen(key));

	if (p == NULL) {
		return 0;
	}
	p += strlen(key);
	return (size_t)strtoul(p, NULL, 10);
}

static int ota_start_session(size_t size)
{
	if (size == 0 || size > CONFIG_OTA_MAX_IMAGE_SIZE) {
		LOG_ERR("OTA rejected: size=%zu (max=%d)", size,
			CONFIG_OTA_MAX_IMAGE_SIZE);
		state = OTA_STATE_ERROR;
		return -EINVAL;
	}

	/* Initialize the flash image context for the slot1 partition. */
	int err = flash_img_init(&flash_ctx);
	if (err != 0) {
		LOG_ERR("flash_img_init failed: %d", err);
		state = OTA_STATE_ERROR;
		return err;
	}
	flash_ctx_initialized = true;

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

	if (bytes_received + len > expected_size) {
		LOG_ERR("OTA overflow: %zu + %zu > %zu", bytes_received, len,
			expected_size);
		state = OTA_STATE_ERROR;
		return -ENOSPC;
	}

	/* Write chunk to flash via the MCUboot flash image context.
	 * flush=false for intermediate chunks; the final chunk is flushed
	 * in ota_finish_session().
	 */
	bool flush = (bytes_received + len >= expected_size);
	int err = flash_img_buffered_write(&flash_ctx, (uint8_t *)data, len,
					   flush);
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
	int err = flash_img_buffered_write(&flash_ctx, NULL, 0, true);
	if (err != 0) {
		LOG_ERR("flash flush failed: %d", err);
		state = OTA_STATE_ERROR;
		return err;
	}

	/* Mark the image for MCUboot swap. */
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

bool ota_handle_publish(struct mqtt_client *client,
			const struct mqtt_publish_param *pub)
{
	ARG_UNUSED(client);

	/* Check if this message is on our cmd topic. */
	if (pub->message.topic.topic.size != strlen(topic_buf) ||
	    memcmp(pub->message.topic.topic.utf8, topic_buf,
		   pub->message.topic.topic.size) != 0) {
		return false;
	}

	const uint8_t *payload = pub->message.payload.data;
	size_t payload_len = pub->message.payload.len;

	/* Detect command messages (JSON-like) vs raw binary chunks. */
	if (memmem(payload, payload_len, OTA_CMD_START,
		   strlen(OTA_CMD_START)) != NULL) {
		size_t size = parse_start_size(payload, payload_len);
		(void)ota_start_session(size);
		return true;
	}

	if (memmem(payload, payload_len, OTA_CMD_FINISH,
		   strlen(OTA_CMD_FINISH)) != NULL) {
		(void)ota_finish_session();
		return true;
	}

	/* Raw binary chunk. */
	if (state == OTA_STATE_RECEIVING) {
		(void)ota_write_chunk(payload, payload_len);
		return true;
	}

	return false;
}

void ota_process(struct mqtt_client *client)
{
	if (!subscribed) {
		return;
	}
	/* MQTT input is driven by telemetry_process() / the caller; nothing
	 * extra needed here for the single-threaded model.
	 */
	ARG_UNUSED(client);
}

enum ota_state ota_get_state(void)
{
	return state;
}

size_t ota_bytes_received(void)
{
	return bytes_received;
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
	/* On native_sim without CONFIG_BOOTLOADER_MCUBOOT, we simulate the
	 * swap marking. The image is already written to the flash simulator;
	 * in a real deployment, MCUboot would read the swap status and
	 * perform the upgrade on reboot.
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
	flash_ctx_initialized = false;
	LOG_DBG("OTA state reset");
}

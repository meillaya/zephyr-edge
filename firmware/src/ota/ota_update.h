/*
 * ota_update.h — MQTT-triggered OTA firmware update (MCUboot + flash simulator).
 *
 * Subscribes to the OTA command topic (cmd/<device_id>/ota by default,
 * configurable via CONFIG_OTA_CMD_TOPIC_FMT) and receives firmware image
 * frames via MQTT PUBLISH messages. Image bytes are written to the flash
 * simulator / OTA slot with stream_flash; once the full image is received,
 * img_manager marks it for boot swap (MCUboot swap-move upgrade path).
 *
 * Wire format (raw binary, one frame per MQTT PUBLISH; docs/OTA.md):
 *   0x01 <uint32_be image_size>   start a session (exactly 5 bytes)
 *   0x02 <chunk bytes>            image data (1..CONFIG_OTA_CHUNK_SIZE bytes)
 *   0x03                          finish: size must match, then mark for swap
 *   0x04                          abort: discard the session state
 *
 * Lifecycle (driven by telemetry.c callbacks):
 *   ota_init();                         // once at startup
 *   telemetry_set_publish_handler(...); // dispatch frames to ota_handle_frame
 *   telemetry_set_connection_handler(...); // ota_on_connected/disconnected
 *
 * The OTA module shares the MQTT client with telemetry; it never owns MQTT
 * input. Inbound frames are read and acknowledged by telemetry.c.
 */
#ifndef ZEPHYR_EDGE_PLATFORM_OTA_UPDATE_H_
#define ZEPHYR_EDGE_PLATFORM_OTA_UPDATE_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/net/mqtt.h>

/* OTA session state. */
enum ota_state {
	OTA_STATE_IDLE = 0,      /* No OTA in progress                    */
	OTA_STATE_RECEIVING,     /* Receiving image chunks                */
	OTA_STATE_COMPLETE,      /* Image received and marked for swap    */
	OTA_STATE_ERROR,         /* OTA failed (size mismatch, flash err) */
};

/* Frame opcodes (first byte of every OTA PUBLISH payload). */
enum ota_frame_opcode {
	OTA_FRAME_START = 0x01,
	OTA_FRAME_DATA = 0x02,
	OTA_FRAME_FINISH = 0x03,
	OTA_FRAME_ABORT = 0x04,
};

/* Start frame: opcode + 4-byte big-endian size. */
#define OTA_START_FRAME_LEN 5
#define OTA_CONTROL_FRAME_LEN 1

/*
 * Initialize the OTA subsystem: build the command topic and reset state.
 * Does not subscribe; the connection handler does that after CONNACK.
 * Returns 0 on success, negative errno on failure.
 */
int ota_init(void);

/* True if topic is exactly the configured OTA command topic. */
bool ota_matches_topic(const struct mqtt_topic *topic);

/*
 * Handle one OTA frame from an MQTT PUBLISH payload. Returns true if the frame
 * belongs to the OTA protocol (it is always consumed); invalid frames move the
 * session to OTA_STATE_ERROR and are logged.
 */
bool ota_handle_frame(const uint8_t *frame, size_t len);

/*
 * Subscribe to the OTA command topic on the shared MQTT client. Called by
 * ota_on_connected() after every CONNACK. Returns 0 on success.
 */
int ota_subscribe(struct mqtt_client *client);

/* Connection callbacks, registered with telemetry_set_connection_handler(). */
void ota_on_connected(struct mqtt_client *client);
void ota_on_disconnected(void);

/* Current OTA session state. */
enum ota_state ota_get_state(void);

/* Total bytes received in the current/last OTA session. */
size_t ota_bytes_received(void);

/* True once ota_subscribe() has queued the subscription for this connection. */
bool ota_is_subscribed(void);

/*
 * Mark the received image for boot swap (MCUboot). Called automatically on
 * finish; exposed for testing.
 */
int ota_mark_for_swap(void);

/* Reset OTA session state to idle (for testing / retry after error). */
void ota_reset(void);

#endif /* ZEPHYR_EDGE_PLATFORM_OTA_UPDATE_H_ */

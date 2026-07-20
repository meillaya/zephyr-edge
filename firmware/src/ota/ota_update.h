/*
 * ota_update.h — MQTT-triggered OTA firmware update (MCUboot + flash simulator).
 *
 * Subscribes to the OTA command topic (devices/<device_id>/cmd by default,
 * configurable via CONFIG_OTA_CMD_TOPIC_FMT) and receives firmware image chunks
 * via MQTT PUBLISH messages. Chunks are written to the flash simulator using
 * the stream_flash API; once the full image is received, img_manager marks it
 * for boot swap (MCUboot swap-move upgrade path).
 *
 * Protocol (simplified for native_sim / CI):
 *   - Publisher sends a PUBLISH to devices/<device_id>/cmd with a JSON-like
 *     payload: {"cmd":"ota_start","size":<N>}
 *   - Subsequent PUBLISHes carry raw binary chunks (up to CONFIG_OTA_CHUNK_SIZE
 *     bytes each) on the same topic.
 *   - Final PUBLISH: {"cmd":"ota_finish"}
 *   - On ota_finish, the image is validated (size match) and marked for swap.
 *
 * Lifecycle:
 *   ota_init();          // once at startup (after telemetry_init)
 *   ota_subscribe();     // subscribe to the cmd topic (after MQTT connect)
 *   ota_process();       // drive MQTT input for OTA messages (call periodically)
 *
 * The OTA module shares the MQTT client context with telemetry (passed at init).
 * If the broker is unreachable, OTA is simply inactive — no error propagation.
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

/*
 * Initialize the OTA subsystem. Prepares the flash simulator and stream flash
 * context. Does not subscribe — call ota_subscribe() after MQTT connect.
 * Returns 0 on success, negative errno on failure.
 */
int ota_init(void);

/*
 * Subscribe to the OTA command topic on the given MQTT client. Must be called
 * after the MQTT client is connected. Returns 0 on success.
 */
int ota_subscribe(struct mqtt_client *client);

/*
 * Handle an incoming MQTT PUBLISH event. Call this from the MQTT event handler
 * when the topic matches the OTA cmd topic. Returns true if the message was
 * consumed by OTA (caller should not process it further).
 */
bool ota_handle_publish(struct mqtt_client *client,
			const struct mqtt_publish_param *pub);

/*
 * Drive OTA processing: call mqtt_input on the client to receive pending
 * messages. Call periodically from the application loop (alongside
 * telemetry_process).
 */
void ota_process(struct mqtt_client *client);

/* Current OTA session state. */
enum ota_state ota_get_state(void);

/* Total bytes received in the current/last OTA session. */
size_t ota_bytes_received(void);

/*
 * Mark the received image for boot swap (MCUboot). Called automatically on
 * ota_finish; exposed for testing.
 */
int ota_mark_for_swap(void);

/* Reset OTA state to idle (for testing / retry after error). */
void ota_reset(void);

#endif /* ZEPHYR_EDGE_PLATFORM_OTA_UPDATE_H_ */

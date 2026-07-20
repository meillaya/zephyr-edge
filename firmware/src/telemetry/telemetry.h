/*
 * telemetry.h — MQTT telemetry publisher (Zephyr MQTT client).
 *
 * Publishes CBOR-encoded telemetry (telemetry_cbor.c) to the broker topic
 * telemetry/<device_id> (format configurable via
 * CONFIG_MQTT_TELEMETRY_TOPIC_FMT) using QoS 1 with retry. The broker address
 * comes from CONFIG_MQTT_BROKER_HOST / CONFIG_MQTT_BROKER_PORT.
 *
 * Lifecycle (driven from the application main loop):
 *   telemetry_init();            // once at startup
 *   telemetry_connect();         // connect to the broker (retries, non-fatal)
 *   ... each cycle ...
 *   telemetry_publish_reading(); // encode + publish one reading
 *   telemetry_process();         // drive keepalive + acks (mqtt_input/live)
 *
 * The publisher degrades gracefully: if the network or broker is unreachable
 * (e.g. native_sim without host TAP/bridge setup), connect/publish return an
 * error and the rest of the application (sensor sampling, logging, power
 * scheduling) continues unaffected.
 */
#ifndef ZEPHYR_EDGE_PLATFORM_TELEMETRY_H_
#define ZEPHYR_EDGE_PLATFORM_TELEMETRY_H_

#include <stdint.h>

#include "power_sched.h"
#include "sensor_log.h"
#include "telemetry_cbor.h"

/*
 * Initialize the MQTT client (buffers, broker address, client id, topic).
 * Does not open a connection. Idempotent. Returns 0 on success.
 */
int telemetry_init(void);

/*
 * Connect to the configured broker. Blocks up to CONFIG_MQTT_CONNECT_TIMEOUT_MS
 * (retrying) waiting for CONNACK. Returns 0 once connected, negative errno on
 * failure (non-fatal to the caller).
 */
int telemetry_connect(void);

/* True once a CONNACK has been received and the session is live. */
bool telemetry_is_connected(void);

/*
 * Return the next monotonic per-device sequence number (telemetry map key 2).
 * Starts at 1 and wraps at UINT32_MAX.
 */
uint32_t telemetry_next_seq(void);

/*
 * Fill msg from a ring-buffer reading plus optional power statistics. Applies
 * the record->telemetry unit conversions (docs/DATA_MODEL.md §2.1) and sets
 * device_id, seq, and timestamp_ms. When stats is non-NULL the power_state and
 * power_counters keys (7,8) are included; otherwise they are omitted.
 */
void telemetry_msg_from_reading(struct telemetry_msg *msg,
				const struct sensor_reading *reading,
				uint32_t seq,
				const struct power_stats *stats,
				uint32_t power_state);

/*
 * Encode msg as CBOR and publish it to the telemetry topic with QoS 1.
 * Returns 0 on success, -ENOTCONN if not connected, or another negative errno
 * on encode/publish failure.
 */
int telemetry_publish(const struct telemetry_msg *msg);

/*
 * Convenience: build a message from reading (+ optional stats) using the next
 * sequence number and publish it. Returns the result of telemetry_publish().
 */
int telemetry_publish_reading(const struct sensor_reading *reading,
			      const struct power_stats *stats,
			      uint32_t power_state);

/*
 * Drive the MQTT state machine: process incoming packets (CONNACK/PUBACK) and
 * send keepalive PINGs when due. Call periodically from the application loop.
 */
void telemetry_process(void);

#endif /* ZEPHYR_EDGE_PLATFORM_TELEMETRY_H_ */

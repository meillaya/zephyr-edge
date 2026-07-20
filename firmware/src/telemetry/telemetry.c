/*
 * telemetry.c — MQTT telemetry publisher (Zephyr MQTT client).
 *
 * Publishes CBOR-encoded telemetry to devices/<device_id>/telemetry using the
 * Zephyr MQTT library over a native POSIX socket (CONFIG_MQTT_LIB +
 * CONFIG_NET_SOCKETS + CONFIG_NET_NATIVE). Mirrors the connection/publish
 * pattern of zephyr/samples/net/mqtt_publisher.
 *
 * Threading model: single application thread. telemetry_process() must be
 * called periodically to drive keepalive and PUBACK handling.
 */

#include <stdio.h>
#include <string.h>
#include <errno.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/mqtt.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/socket.h>

#include "telemetry.h"

LOG_MODULE_REGISTER(TELEMETRY, CONFIG_LOG_DEFAULT_LEVEL);

/* MQTT client RX/TX buffers. Sized for one CBOR telemetry payload plus MQTT
 * packet overhead (fixed header + topic + QoS1 acks).
 */
#define TELEMETRY_MQTT_BUF_SIZE 512

/* Maximum encoded CBOR payload size (well above the ~60-byte telemetry map). */
#define TELEMETRY_PAYLOAD_BUF_SIZE 192

/* Topic buffer: format string + device id. */
#define TELEMETRY_TOPIC_BUF_SIZE 64

static uint8_t rx_buffer[TELEMETRY_MQTT_BUF_SIZE];
static uint8_t tx_buffer[TELEMETRY_MQTT_BUF_SIZE];
static uint8_t payload_buffer[TELEMETRY_PAYLOAD_BUF_SIZE];
static char topic_buffer[TELEMETRY_TOPIC_BUF_SIZE];

static struct mqtt_client client_ctx;
static struct sockaddr_storage broker;

static bool connected;
static uint32_t seq_counter;

/* ── Broker / client setup ─────────────────────────────────────────── */

static void broker_init(void)
{
	struct sockaddr_in *broker4 = (struct sockaddr_in *)&broker;

	memset(&broker, 0, sizeof(broker));
	broker4->sin_family = AF_INET;
	broker4->sin_port = htons(CONFIG_MQTT_BROKER_PORT);
	if (zsock_inet_pton(AF_INET, CONFIG_MQTT_BROKER_HOST, &broker4->sin_addr) != 1) {
		LOG_ERR("invalid broker host: \"%s\"", CONFIG_MQTT_BROKER_HOST);
	}
}

/* ── MQTT event handler ────────────────────────────────────────────── */

static void telemetry_evt_handler(struct mqtt_client *client,
				  const struct mqtt_evt *evt)
{
	ARG_UNUSED(client);

	switch (evt->type) {
	case MQTT_EVT_CONNACK:
		if (evt->result == 0) {
			connected = true;
			LOG_INF("mqtt connected to %s:%d", CONFIG_MQTT_BROKER_HOST,
				CONFIG_MQTT_BROKER_PORT);
		} else {
			connected = false;
			LOG_ERR("mqtt CONNACK refused: %d", evt->result);
		}
		break;

	case MQTT_EVT_DISCONNECT:
		connected = false;
		LOG_WRN("mqtt disconnected: %d", evt->result);
		break;

	case MQTT_EVT_PUBACK:
		if (evt->result == 0) {
			LOG_DBG("mqtt PUBACK id=%u", evt->param.puback.message_id);
		} else {
			LOG_WRN("mqtt PUBACK failed: %d", evt->result);
		}
		break;

	default:
		break;
	}
}

/* ── Public API ────────────────────────────────────────────────────── */

int telemetry_init(void)
{
	mqtt_client_init(&client_ctx);

	broker_init();

	client_ctx.broker = &broker;
	client_ctx.evt_cb = telemetry_evt_handler;
	client_ctx.client_id.utf8 = (const uint8_t *)CONFIG_DEVICE_ID;
	client_ctx.client_id.size = strlen(CONFIG_DEVICE_ID);
	client_ctx.user_name = NULL;
	client_ctx.password = NULL;
	client_ctx.protocol_version = MQTT_VERSION_3_1_1;

	client_ctx.rx_buf = rx_buffer;
	client_ctx.rx_buf_size = sizeof(rx_buffer);
	client_ctx.tx_buf = tx_buffer;
	client_ctx.tx_buf_size = sizeof(tx_buffer);

	/* Build the publish topic: devices/<device_id>/telemetry by default. */
	int n = snprintf(topic_buffer, sizeof(topic_buffer),
			 CONFIG_MQTT_TELEMETRY_TOPIC_FMT, CONFIG_DEVICE_ID);

	if (n < 0 || (size_t)n >= sizeof(topic_buffer)) {
		LOG_ERR("telemetry topic too long");
		return -ENOMEM;
	}

	connected = false;
	seq_counter = 0;

	LOG_INF("telemetry init: client_id=\"%s\" topic=\"%s\"", CONFIG_DEVICE_ID,
		topic_buffer);
	return 0;
}

int telemetry_connect(void)
{
	if (connected) {
		return 0;
	}

	int err = mqtt_connect(&client_ctx);

	if (err != 0) {
		LOG_WRN("mqtt_connect failed: %d", err);
		return err;
	}

	/* Drive the socket until CONNACK arrives or the timeout elapses. */
	int64_t deadline = k_uptime_get() + CONFIG_MQTT_CONNECT_TIMEOUT_MS;

	while (!connected && k_uptime_get() < deadline) {
		mqtt_input(&client_ctx);
		k_sleep(K_MSEC(50));
	}

	if (!connected) {
		LOG_WRN("mqtt connect timed out after %d ms",
			CONFIG_MQTT_CONNECT_TIMEOUT_MS);
		mqtt_abort(&client_ctx);
		return -ETIMEDOUT;
	}

	return 0;
}

bool telemetry_is_connected(void)
{
	return connected;
}

uint32_t telemetry_next_seq(void)
{
	seq_counter++;
	if (seq_counter == 0) { /* wrap past zero: seq is monotonic, never 0 */
		seq_counter = 1;
	}
	return seq_counter;
}

void telemetry_msg_from_reading(struct telemetry_msg *msg,
				const struct sensor_reading *reading,
				uint32_t seq,
				const struct power_stats *stats,
				uint32_t power_state)
{
	memset(msg, 0, sizeof(*msg));

	msg->device_id = CONFIG_DEVICE_ID;
	msg->seq = seq;
	msg->timestamp_ms = (uint64_t)reading->timestamp_ms;

	/* Record -> telemetry unit conversions (docs/DATA_MODEL.md §2.1). */
	msg->temperature_c = (double)reading->temperature_mc / 1000.0;
	msg->humidity_pct = (double)reading->humidity_mpct / 1000.0;
	msg->pressure_kpa = (double)reading->pressure_pa / 1000.0;

	if (stats != NULL) {
		msg->has_power = true;
		msg->power_state = power_state;
		msg->active_ms = stats->active_ms;
		msg->idle_ms = stats->idle_ms;
		msg->sleep_ms = stats->sleep_ms;
	}
}

int telemetry_publish(const struct telemetry_msg *msg)
{
	if (!connected) {
		return -ENOTCONN;
	}

	size_t len = 0;
	int err = telemetry_cbor_encode(msg, payload_buffer,
					sizeof(payload_buffer), &len);

	if (err != 0) {
		LOG_ERR("cbor encode failed: %d", err);
		return err;
	}

	/* QoS1 message id must be non-zero; derive it from the payload seq. */
	uint16_t message_id = (uint16_t)(msg->seq & 0xFFFF);

	if (message_id == 0) {
		message_id = 1;
	}

	struct mqtt_publish_param param = {0};

	param.message.topic.qos = MQTT_QOS_1_AT_LEAST_ONCE;
	param.message.topic.topic.utf8 = (uint8_t *)topic_buffer;
	param.message.topic.topic.size = strlen(topic_buffer);
	param.message.payload.data = payload_buffer;
	param.message.payload.len = (uint32_t)len;
	param.message_id = message_id;
	param.dup_flag = 0U;
	param.retain_flag = 0U;

	err = mqtt_publish(&client_ctx, &param);
	if (err != 0) {
		LOG_WRN("mqtt_publish failed: %d", err);
		return err;
	}

	LOG_DBG("published seq=%u len=%u to %s", msg->seq, (unsigned int)len,
		topic_buffer);
	return 0;
}

int telemetry_publish_reading(const struct sensor_reading *reading,
			      const struct power_stats *stats,
			      uint32_t power_state)
{
	struct telemetry_msg msg;

	telemetry_msg_from_reading(&msg, reading, telemetry_next_seq(), stats,
				   power_state);
	return telemetry_publish(&msg);
}

void telemetry_process(void)
{
	if (!connected) {
		return;
	}

	mqtt_input(&client_ctx);
	mqtt_live(&client_ctx);
}

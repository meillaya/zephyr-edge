/*
 * telemetry.c — MQTT telemetry publisher (Zephyr MQTT client).
 *
 * Publishes CBOR-encoded telemetry to telemetry/<device_id> using the
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
#include <zephyr/sys/util.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/socket.h>

#include "telemetry.h"

#if defined(CONFIG_MQTT_TLS)
#include <zephyr/net/tls_credentials.h>

#include "telemetry_ca_cert.h"

#define TELEMETRY_CA_TAG 1
static sec_tag_t telemetry_sec_tags[] = { TELEMETRY_CA_TAG };
#endif

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
static uint16_t packet_id_counter;
static telemetry_publish_fn publish_handler;
static telemetry_connection_fn connection_handler;

/* Inbound PUBLISH payload scratch buffer (OTA frames are the only subscriber). */
static uint8_t publish_buffer[CONFIG_TELEMETRY_RX_PAYLOAD_MAX];

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

/* ── Inbound PUBLISH handling ──────────────────────────────────────── */

/*
 * Read an inbound PUBLISH payload into publish_buffer. Zephyr's MQTT event
 * carries only the payload length, so the bytes must be read from the socket
 * here. Oversized payloads are drained (so the session stays in sync) and
 * reported as -EMSGSIZE; I/O errors are reported as -EIO.
 */
static int read_publish_payload(struct mqtt_client *client,
				const struct mqtt_publish_param *pub,
				size_t *out_len)
{
	size_t len = pub->message.payload.len;
	size_t to_read = MIN(len, sizeof(publish_buffer));
	int err = mqtt_readall_publish_payload(client, publish_buffer, to_read);

	if (err != 0) {
		LOG_WRN("failed to read PUBLISH payload: %d", err);
		return -EIO;
	}

	size_t remaining = len - to_read;
	uint8_t discard[64];

	while (remaining > 0) {
		size_t chunk = MIN(remaining, sizeof(discard));

		err = mqtt_readall_publish_payload(client, discard, chunk);
		if (err != 0) {
			LOG_WRN("failed to drain oversized PUBLISH: %d", err);
			return -EIO;
		}
		remaining -= chunk;
	}

	if (len > sizeof(publish_buffer)) {
		LOG_WRN("inbound PUBLISH too large: %zu > %zu", len,
			sizeof(publish_buffer));
		return -EMSGSIZE;
	}

	*out_len = len;
	return 0;
}

static void handle_publish(struct mqtt_client *client,
			   const struct mqtt_publish_param *pub)
{
	size_t len = 0;
	int rc = read_publish_payload(client, pub, &len);

	if (rc == 0) {
		bool consumed = false;

		if (publish_handler != NULL) {
			consumed = publish_handler(&pub->message.topic,
						   publish_buffer, len);
		}
		if (!consumed) {
			LOG_DBG("unhandled PUBLISH (%zu bytes)", len);
		}
	}

	/* Acknowledge QoS1 even for an oversized/unknown frame, so the broker
	 * does not redeliver it forever. Skip the ack only if the socket read
	 * itself failed (the session is likely already broken).
	 */
	if (rc != -EIO &&
	    pub->message.topic.qos == MQTT_QOS_1_AT_LEAST_ONCE) {
		struct mqtt_puback_param ack = {
			.message_id = pub->message_id,
		};

		int ack_rc = mqtt_publish_qos1_ack(client, &ack);

		if (ack_rc != 0) {
			LOG_WRN("mqtt PUBACK send failed: %d", ack_rc);
		}
	}
}

/* ── MQTT event handler ────────────────────────────────────────────── */

static void telemetry_evt_handler(struct mqtt_client *client,
				  const struct mqtt_evt *evt)
{
	switch (evt->type) {
	case MQTT_EVT_CONNACK:
		connected = (evt->result == 0);
		if (connected) {
			LOG_INF("mqtt connected to %s:%d", CONFIG_MQTT_BROKER_HOST,
				CONFIG_MQTT_BROKER_PORT);
		} else {
			LOG_ERR("mqtt CONNACK refused: %d", evt->result);
		}
		if (connection_handler != NULL) {
			connection_handler(connected);
		}
		break;

	case MQTT_EVT_DISCONNECT:
		connected = false;
		LOG_WRN("mqtt disconnected: %d", evt->result);
		if (connection_handler != NULL) {
			connection_handler(false);
		}
		break;

	case MQTT_EVT_PUBACK:
		if (evt->result == 0) {
			LOG_DBG("mqtt PUBACK id=%u", evt->param.puback.message_id);
		} else {
			LOG_WRN("mqtt PUBACK failed: %d", evt->result);
		}
		break;

	case MQTT_EVT_PUBLISH:
		handle_publish(client, &evt->param.publish);
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

	if (CONFIG_MQTT_USERNAME[0] != '\0') {
		static struct mqtt_utf8 user_name;

		user_name.utf8 = (const uint8_t *)CONFIG_MQTT_USERNAME;
		user_name.size = strlen(CONFIG_MQTT_USERNAME);
		client_ctx.user_name = &user_name;
	}
	if (CONFIG_MQTT_PASSWORD[0] != '\0') {
		static struct mqtt_utf8 password;

		password.utf8 = (const uint8_t *)CONFIG_MQTT_PASSWORD;
		password.size = strlen(CONFIG_MQTT_PASSWORD);
		client_ctx.password = &password;
	}

#if defined(CONFIG_MQTT_TLS)
	int tls_err = tls_credential_add(TELEMETRY_CA_TAG,
					 TLS_CREDENTIAL_CA_CERTIFICATE,
					 telemetry_ca_cert,
					 TELEMETRY_CA_CERT_LEN);

	if (tls_err < 0 && tls_err != -EEXIST) {
		LOG_ERR("failed to register MQTT CA certificate: %d", tls_err);
		return tls_err;
	}
	LOG_INF("MQTT TLS configured: ca=%u bytes tag=%d server=%s",
		(unsigned int)TELEMETRY_CA_CERT_LEN, TELEMETRY_CA_TAG,
		CONFIG_MQTT_TLS_SERVER_NAME);

	client_ctx.transport.type = MQTT_TRANSPORT_SECURE;

	struct mqtt_sec_config *tls = &client_ctx.transport.tls.config;

	tls->peer_verify = TLS_PEER_VERIFY_REQUIRED;
	tls->cipher_list = NULL;
	tls->cipher_count = 0;
	tls->sec_tag_list = telemetry_sec_tags;
	tls->sec_tag_count = ARRAY_SIZE(telemetry_sec_tags);
	tls->hostname = CONFIG_MQTT_TLS_SERVER_NAME;
#else
	client_ctx.transport.type = MQTT_TRANSPORT_NON_SECURE;
#endif

	client_ctx.rx_buf = rx_buffer;
	client_ctx.rx_buf_size = sizeof(rx_buffer);
	client_ctx.tx_buf = tx_buffer;
	client_ctx.tx_buf_size = sizeof(tx_buffer);

	/* Build the publish topic: telemetry/<device_id> by default. */
	int n = snprintf(topic_buffer, sizeof(topic_buffer),
			 CONFIG_MQTT_TELEMETRY_TOPIC_FMT, CONFIG_DEVICE_ID);

	if (n < 0 || (size_t)n >= sizeof(topic_buffer)) {
		LOG_ERR("telemetry topic too long");
		return -ENOMEM;
	}

	connected = false;
	seq_counter = 0;
	packet_id_counter = 0;

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
		mqtt_abort(&client_ctx);
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

struct mqtt_client *telemetry_client(void)
{
	return &client_ctx;
}

void telemetry_set_publish_handler(telemetry_publish_fn handler)
{
	publish_handler = handler;
}

void telemetry_set_connection_handler(telemetry_connection_fn handler)
{
	connection_handler = handler;
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

	/* QoS1 packet ids are independent of the telemetry seq; the broker only
	 * requires that an id is non-zero while a PUBACK is outstanding.
	 */
	packet_id_counter++;
	if (packet_id_counter == 0) {
		packet_id_counter = 1;
	}
	uint16_t message_id = packet_id_counter;

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
	if (connected) {
		mqtt_live(&client_ctx);
	}
}

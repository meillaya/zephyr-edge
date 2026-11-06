/*
 * main.c — zephyr-edge-platform application loop.
 *
 * Wires the platform subsystems together:
 *   1. Custom BME280 I2C sensor driver  (drivers/sensor/bme280_custom)
 *   2. Ring-buffered sensor logging      (sensor_log.c)
 *   3. Low-power sleep/wake scheduler    (power_sched.c)
 *   4. MQTT/CBOR telemetry publisher     (telemetry/, Phase 2)
 *   5. MQTT-triggered OTA receiver       (ota/ota_update.c, Phase 3)
 *   6. Kconfig-gated fault-injection hooks (fault_inject.c)
 *
 * Each wake cycle, the power scheduler invokes an active-window callback that
 * samples the BME280 and pushes a timestamped reading into the ring buffer.
 * The main loop then drains the ring buffer and publishes each reading as a
 * CBOR-encoded MQTT telemetry message (docs/DATA_MODEL.md §1) to
 * telemetry/<device_id>, then services the MQTT connection (keepalive,
 * PUBACK, inbound OTA frames) and logs power-state statistics.
 *
 * During the sleep window the scheduler calls telemetry_process() every
 * CONFIG_POWER_SERVICE_INTERVAL_MS so OTA frames and keepalive are handled even
 * while the CPU is mostly asleep.
 *
 * Telemetry is best-effort: if the broker is unreachable (e.g. native_sim
 * without host TAP/bridge setup), readings stay queued in the ring buffer and
 * the sensor / logging / power loop continues unaffected.
 *
 * Log values are printed in integer milli-units to avoid pulling in floating
 * point printf support on the console.
 */

#include <stdio.h>

#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "fault_inject.h"
#include "ota/ota_update.h"
#include "power_sched.h"
#include "sensor_log.h"
#include "telemetry.h"

LOG_MODULE_REGISTER(APP, CONFIG_LOG_DEFAULT_LEVEL);

static const struct device *const bme280 =
	DEVICE_DT_GET_ONE(vendor_bme280_custom);

/* One reading retained across a failed publish attempt so it is not lost. */
static struct sensor_reading pending_reading;
static bool pending_valid;
static uint32_t telemetry_dropped;

/* Active-window callback: sample the BME280 and store a reading. */
static void sample_and_log(void *user_data)
{
	ARG_UNUSED(user_data);
	struct sensor_value temp;
	struct sensor_value hum;
	struct sensor_value press;

	if (fault_inject_sensor_check() != 0) {
		LOG_WRN("sensor fault injected — skipping sample");
		return;
	}

	if (sensor_sample_fetch(bme280) < 0) {
		LOG_ERR("BME280 sample_fetch failed");
		return;
	}
	if (sensor_channel_get(bme280, SENSOR_CHAN_AMBIENT_TEMP, &temp) < 0 ||
	    sensor_channel_get(bme280, SENSOR_CHAN_HUMIDITY, &hum) < 0 ||
	    sensor_channel_get(bme280, SENSOR_CHAN_PRESS, &press) < 0) {
		LOG_ERR("BME280 channel_get failed");
		return;
	}

	struct sensor_reading reading = {
		.timestamp_ms = k_uptime_get(),
		.temperature_mc = (int32_t)(sensor_value_to_double(&temp) * 1000.0),
		.humidity_mpct = (int32_t)(sensor_value_to_double(&hum) * 1000.0),
		.pressure_pa = (int32_t)(sensor_value_to_double(&press) * 1000.0),
	};

	if (sensor_log_put(&reading) == 0) {
		LOG_INF("sample: T=%d mC  H=%d m%%RH  P=%d Pa", reading.temperature_mc,
			reading.humidity_mpct, reading.pressure_pa);
	}
}

/* Sleep-window service callback: keep the MQTT session responsive. */
static void service_telemetry(void *user_data)
{
	ARG_UNUSED(user_data);
	telemetry_process();
}

/*
 * Publish one reading. Returns 0 if the reading was consumed (published or
 * dropped by fault injection), negative errno if it must be retried.
 */
static int publish_reading(const struct sensor_reading *reading,
			   const struct power_stats *stats)
{
	if (fault_inject_should_drop_packet()) {
		telemetry_dropped++;
		LOG_DBG("packet fault injected — dropping publish");
		return 0;
	}

	int rc = telemetry_publish_reading(reading, stats,
					   (uint32_t)power_sched_state());

	if (rc != 0) {
		LOG_WRN("telemetry publish failed: %d", rc);
		return rc;
	}

	return 0;
}

/*
 * Drain the ring buffer and publish each reading as CBOR telemetry. Attempts a
 * (re)connect once per call when disconnected; if the broker is still
 * unreachable the ring buffer is left intact for the next cycle.
 */
static void drain_and_publish(void)
{
	struct power_stats st;

	if (!telemetry_is_connected() && telemetry_connect() != 0) {
		/* Keep readings queued instead of popping and discarding them. */
		telemetry_process();
		return;
	}

	power_sched_get_stats(&st);

	if (pending_valid) {
		if (publish_reading(&pending_reading, &st) != 0) {
			return;
		}
		LOG_INF("[telemetry] retried pending reading ts=%lld ms",
			(long long)pending_reading.timestamp_ms);
		pending_valid = false;
	}

	struct sensor_reading reading;

	while (sensor_log_get(&reading) == 0) {
		if (publish_reading(&reading, &st) != 0) {
			pending_reading = reading;
			pending_valid = true;
			break;
		}
		LOG_INF("[telemetry] published ts=%lld ms T=%d mC H=%d m%%RH "
			"P=%d Pa",
			(long long)reading.timestamp_ms, reading.temperature_mc,
			reading.humidity_mpct, reading.pressure_pa);
	}

	/* Drive keepalive + PUBACK + inbound PUBLISH processing. */
	telemetry_process();
}

/* Dispatch inbound OTA frames from the shared MQTT client. */
static bool on_telemetry_publish(const struct mqtt_topic *topic,
				 const uint8_t *payload, size_t len)
{
	if (!ota_matches_topic(topic)) {
		return false;
	}
	return ota_handle_frame(payload, len);
}

static void on_telemetry_connection(bool connected)
{
	if (connected) {
		ota_on_connected(telemetry_client());
	} else {
		ota_on_disconnected();
	}
}

int main(void)
{
	LOG_INF("zephyr-edge-platform starting (device_id=\"%s\")", CONFIG_DEVICE_ID);

	if (!device_is_ready(bme280)) {
		LOG_ERR("BME280 device not ready");
		return -ENODEV;
	}

	sensor_log_init();
	fault_inject_init();
	power_sched_init(sample_and_log, NULL);
	if (power_sched_set_service(service_telemetry, NULL, 0) != 0) {
		LOG_ERR("failed to register telemetry service callback");
		return -EINVAL;
	}

	/* MQTT telemetry + OTA receiver (best-effort connect). */
	telemetry_init();
	if (ota_init() != 0) {
		LOG_ERR("OTA init failed; OTA disabled");
	}
	telemetry_set_publish_handler(on_telemetry_publish);
	telemetry_set_connection_handler(on_telemetry_connection);
	(void)telemetry_connect();

	LOG_INF("entering wake/sample/log/sleep loop: %d ms active + %d ms idle + "
		"%d ms sleep per cycle; service every %d ms",
		CONFIG_POWER_ACTIVE_MS, CONFIG_POWER_IDLE_MS, CONFIG_POWER_SLEEP_MS,
		CONFIG_POWER_SERVICE_INTERVAL_MS);

	while (1) {
		power_sched_run_cycles(1);
		drain_and_publish();

		struct power_stats st;

		power_sched_get_stats(&st);
		LOG_INF("power: cycles=%u active=%llu ms idle=%llu ms sleep=%llu ms "
			"(ring_dropped=%u pub_dropped=%u)",
			st.cycles, (unsigned long long)st.active_ms,
			(unsigned long long)st.idle_ms,
			(unsigned long long)st.sleep_ms, sensor_log_dropped(),
			telemetry_dropped);
	}

	return 0;
}

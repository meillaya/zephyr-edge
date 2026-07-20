/*
 * main.c — zephyr-edge-platform application loop.
 *
 * Wires the platform subsystems together:
 *   1. Custom BME280 I2C sensor driver  (drivers/sensor/bme280_custom)
 *   2. Ring-buffered sensor logging      (sensor_log.c)
 *   3. Low-power sleep/wake scheduler    (power_sched.c)
 *   4. MQTT telemetry publisher          (telemetry/, Phase 2)
 *
 * Each wake cycle, the power scheduler invokes an active-window callback that
 * samples the BME280 and pushes a timestamped reading into the ring buffer.
 * The main loop then drains the ring buffer and publishes each reading as a
 * CBOR-encoded MQTT telemetry message (docs/DATA_MODEL.md §1) to
 * telemetry/<device_id>, then services the MQTT connection (keepalive
 * + PUBACK handling) and logs power-state statistics.
 *
 * Telemetry is best-effort: if the broker is unreachable (e.g. native_sim
 * without host TAP/bridge setup), publishing is skipped and the sensor /
 * logging / power loop continues unaffected.
 *
 * Log values are printed in integer milli-units to avoid pulling in floating
 * point printf support on the console.
 */

#include <stdio.h>

#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "power_sched.h"
#include "sensor_log.h"
#include "telemetry.h"

LOG_MODULE_REGISTER(APP, CONFIG_LOG_DEFAULT_LEVEL);

static const struct device *const bme280 =
	DEVICE_DT_GET_ONE(vendor_bme280_custom);

/* Active-window callback: sample the BME280 and store a reading. */
static void sample_and_log(void *user_data)
{
	ARG_UNUSED(user_data);
	struct sensor_value temp;
	struct sensor_value hum;
	struct sensor_value press;

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

/*
 * Drain the ring buffer and publish each reading as CBOR telemetry. Attempts a
 * (re)connect once per call when disconnected, then services the MQTT session.
 */
static void drain_and_publish(void)
{
	struct power_stats st;

	power_sched_get_stats(&st);

	if (!telemetry_is_connected()) {
		(void)telemetry_connect(); /* best-effort, non-fatal */
	}

	struct sensor_reading reading;

	while (sensor_log_get(&reading) == 0) {
		int rc = telemetry_publish_reading(&reading, &st,
						   (uint32_t)power_sched_state());

		if (rc == 0) {
			LOG_INF("[telemetry] published ts=%lld ms T=%d mC H=%d m%%RH "
				"P=%d Pa",
				reading.timestamp_ms, reading.temperature_mc,
				reading.humidity_mpct, reading.pressure_pa);
		} else {
			LOG_INF("[telemetry] not published (rc=%d) ts=%lld ms T=%d mC "
				"H=%d m%%RH P=%d Pa",
				rc, reading.timestamp_ms, reading.temperature_mc,
				reading.humidity_mpct, reading.pressure_pa);
		}
	}

	/* Drive keepalive + PUBACK processing for the MQTT session. */
	telemetry_process();
}

int main(void)
{
	LOG_INF("zephyr-edge-platform starting (device_id=\"%s\")", CONFIG_DEVICE_ID);

	if (!device_is_ready(bme280)) {
		LOG_ERR("BME280 device not ready");
		return 0;
	}

	sensor_log_init();
	power_sched_init(sample_and_log, NULL);

	/* Phase 2: MQTT telemetry publisher (best-effort connect). */
	telemetry_init();
	(void)telemetry_connect();

	LOG_INF("entering wake/sample/log/sleep loop: %d ms active + %d ms idle + "
		"%d ms sleep per cycle",
		CONFIG_POWER_ACTIVE_MS, CONFIG_POWER_IDLE_MS, CONFIG_POWER_SLEEP_MS);

	while (1) {
		power_sched_run_cycles(1);
		drain_and_publish();

		struct power_stats st;

		power_sched_get_stats(&st);
		LOG_INF("power: cycles=%u active=%llu ms idle=%llu ms sleep=%llu ms "
			"(dropped=%u)",
			st.cycles, st.active_ms, st.idle_ms, st.sleep_ms,
			sensor_log_dropped());
	}

	return 0;
}

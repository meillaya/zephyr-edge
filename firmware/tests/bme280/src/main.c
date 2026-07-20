/*
 * main.c — ztest suite for the custom BME280 I2C driver on native_sim.
 *
 * Validates:
 *   1. The driver binds and the (emulated) device is ready.
 *   2. A full I2C round-trip: sample_fetch reads calibration + raw data from
 *      the emulator and channel_get returns the compensated values, which the
 *      emulator fixture makes exactly 25.0 C / 50.0 %RH / 101.325 kPa.
 *   3. The Bosch compensation math against hand-derived known-answer vectors.
 */

#include <math.h>

#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/ztest.h>

#include "bme280_custom.h"

static const struct device *const bme280 =
	DEVICE_DT_GET_ONE(vendor_bme280_custom);

ZTEST(bme280_tests, test_device_ready)
{
	zassert_true(device_is_ready(bme280), "BME280 device is not ready");
}

ZTEST(bme280_tests, test_sample_fetch_and_channel_get)
{
	struct sensor_value temp;
	struct sensor_value hum;
	struct sensor_value press;

	zassert_ok(sensor_sample_fetch(bme280), "sample_fetch failed");

	zassert_ok(sensor_channel_get(bme280, SENSOR_CHAN_AMBIENT_TEMP, &temp));
	zassert_ok(sensor_channel_get(bme280, SENSOR_CHAN_HUMIDITY, &hum));
	zassert_ok(sensor_channel_get(bme280, SENSOR_CHAN_PRESS, &press));

	double t = sensor_value_to_double(&temp);
	double h = sensor_value_to_double(&hum);
	double p = sensor_value_to_double(&press);

	/* Emulator fixture compensates to these exact values. */
	zassert_true(fabs(t - 25.0) < 0.01, "temperature %.4f != 25.0 C", t);
	zassert_true(fabs(h - 50.0) < 0.01, "humidity %.4f != 50.0 %%RH", h);
	zassert_true(fabs(p - 101.325) < 0.01, "pressure %.4f != 101.325 kPa", p);
}

ZTEST(bme280_tests, test_unsupported_channel_rejected)
{
	struct sensor_value val;

	zassert_ok(sensor_sample_fetch(bme280));
	/* SENSOR_CHAN_VOLTAGE is not provided by this driver. */
	zassert_equal(sensor_channel_get(bme280, SENSOR_CHAN_VOLTAGE, &val),
		      -ENOTSUP, "expected -ENOTSUP for unsupported channel");
}

ZTEST(bme280_tests, test_compensation_known_answer)
{
	struct bme280_calib calib = {0};
	double t_fine;

	/*
	 * Temperature: dig_T1=0, dig_T2=5120, dig_T3=0, adc_T=409600
	 *   var1 = (409600/16384 - 0) * 5120 = 128000
	 *   var2 = 0 (dig_T3 = 0)
	 *   t_fine = 128000, T = 128000/5120 = 25.0 C
	 */
	calib.dig_T2 = 5120;
	double t = bme280_compensate_temperature(409600, &calib, &t_fine);

	zassert_true(fabs(t - 25.0) < 1e-6, "T=%.6f != 25.0", t);
	zassert_true(fabs(t_fine - 128000.0) < 1e-6, "t_fine=%.6f != 128000",
		     t_fine);

	/*
	 * Pressure: dig_P1=6250, all other dig_P=0, adc_P=947251, t_fine=128000
	 *   var1(press) collapses to dig_P1=6250; p = (1048576-947251) = 101325 Pa
	 */
	calib.dig_P1 = 6250;
	double p = bme280_compensate_pressure(947251, &calib, t_fine);

	zassert_true(fabs(p - 101325.0) < 1e-3, "P=%.6f != 101325 Pa", p);

	/*
	 * Humidity: dig_H2=16384, all other dig_H=0, adc_H=200, t_fine=128000
	 *   h = (200 - 0) * (16384/65536) = 200 * 0.25 = 50.0 %RH
	 */
	calib.dig_H2 = 16384;
	double h = bme280_compensate_humidity(200, &calib, t_fine);

	zassert_true(fabs(h - 50.0) < 1e-6, "H=%.6f != 50.0", h);
}

ZTEST_SUITE(bme280_tests, NULL, NULL, NULL, NULL, NULL);

/*
 * bme280_custom.c — Custom Bosch BME280 I2C sensor driver.
 *
 * Implements the Zephyr sensor subsystem API (sample_fetch / channel_get) for
 * the BME280 temperature/humidity/pressure sensor over I2C. Calibration and raw
 * ADC data are read from the device register file and converted with the Bosch
 * datasheet floating-point compensation formulas.
 *
 * Channels provided:
 *   SENSOR_CHAN_AMBIENT_TEMP -> degrees Celsius
 *   SENSOR_CHAN_HUMIDITY     -> percent relative humidity
 *   SENSOR_CHAN_PRESS        -> kilopascals (Zephyr convention)
 *
 * Devicetree compatible: "vendor,bme280-custom" (see binding yaml).
 */

#define DT_DRV_COMPAT vendor_bme280_custom

#include <errno.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "bme280_custom.h"

LOG_MODULE_REGISTER(BME280_CUSTOM, CONFIG_SENSOR_LOG_LEVEL);

/* Bound for the forced-measurement completion poll (see sample_fetch). */
#define BME280_MEAS_POLL_MAX   100
#define BME280_MEAS_POLL_DELAY_MS 1

struct bme280_config {
	struct i2c_dt_spec i2c;
};

struct bme280_data {
	struct bme280_calib calib;
	double t_fine;
	double temp_c;
	double hum_pct;
	double press_kpa;
	int32_t adc_T;
	int32_t adc_P;
	int32_t adc_H;
};

/* ------------------------------------------------------------------ */
/* Bosch floating-point compensation (datasheet §4.2.3)                */
/* ------------------------------------------------------------------ */

double bme280_compensate_temperature(int32_t adc_T, const struct bme280_calib *c,
				     double *t_fine)
{
	double var1 = (((double)adc_T) / 16384.0 - ((double)c->dig_T1) / 1024.0) *
		      ((double)c->dig_T2);
	double var2 = ((((double)adc_T) / 131072.0 - ((double)c->dig_T1) / 8192.0) *
		       (((double)adc_T) / 131072.0 - ((double)c->dig_T1) / 8192.0)) *
		      ((double)c->dig_T3);

	*t_fine = var1 + var2;
	return *t_fine / 5120.0;
}

double bme280_compensate_pressure(int32_t adc_P, const struct bme280_calib *c,
				  double t_fine)
{
	double var1 = ((double)t_fine) / 2.0 - 64000.0;
	double var2 = var1 * var1 * ((double)c->dig_P6) / 32768.0;

	var2 = var2 + var1 * ((double)c->dig_P5) * 2.0;
	var2 = (var2 / 4.0) + ((double)c->dig_P4) * 65536.0;
	var1 = (((double)c->dig_P3) * var1 * var1 / 524288.0 +
		((double)c->dig_P2) * var1) / 524288.0;
	var1 = (1.0 + var1 / 32768.0) * ((double)c->dig_P1);
	if (var1 == 0.0) {
		return 0.0; /* avoid division by zero */
	}

	double p = 1048576.0 - (double)adc_P;

	p = (p - (var2 / 4096.0)) * 6250.0 / var1;
	var1 = ((double)c->dig_P9) * p * p / 2147483648.0;
	var2 = p * ((double)c->dig_P8) / 32768.0;
	p = p + (var1 + var2 + ((double)c->dig_P7)) / 16.0;
	return p; /* Pa */
}

double bme280_compensate_humidity(int32_t adc_H, const struct bme280_calib *c,
				  double t_fine)
{
	double h = ((double)t_fine) - 76800.0;

	h = (((double)adc_H) -
	     (((double)c->dig_H4) * 64.0 + ((double)c->dig_H5) / 16384.0 * h)) *
	    (((double)c->dig_H2) / 65536.0 *
	     (1.0 + ((double)c->dig_H6) / 67108864.0 * h *
	      (1.0 + ((double)c->dig_H3) / 67108864.0 * h)));
	h = h * (1.0 - ((double)c->dig_H1) * h / 524288.0);

	if (h > 100.0) {
		h = 100.0;
	} else if (h < 0.0) {
		h = 0.0;
	}
	return h;
}

/* ------------------------------------------------------------------ */
/* I2C register helpers                                                */
/* ------------------------------------------------------------------ */

static int bme280_reg_write(const struct i2c_dt_spec *i2c, uint8_t reg, uint8_t val)
{
	return i2c_burst_write_dt(i2c, reg, &val, sizeof(val));
}

static int bme280_burst_read(const struct i2c_dt_spec *i2c, uint8_t reg, uint8_t *buf,
			     size_t len)
{
	return i2c_burst_read_dt(i2c, reg, buf, len);
}

/* Read and decode the full calibration block (datasheet §4.2.2). */
static int bme280_read_calib(const struct device *dev)
{
	const struct bme280_config *cfg = dev->config;
	struct bme280_data *data = dev->data;
	uint8_t tp[26]; /* 0x88..0x9F: dig_T1..dig_P9 */
	uint8_t h[7];   /* 0xE1..0xE7: dig_H2..dig_H6 */
	int rc;

	rc = bme280_burst_read(&cfg->i2c, BME280_REG_CALIB_TP, tp, sizeof(tp));
	if (rc < 0) {
		return rc;
	}
	rc = bme280_burst_read(&cfg->i2c, BME280_REG_CALIB_H2H6, h, sizeof(h));
	if (rc < 0) {
		return rc;
	}
	rc = bme280_burst_read(&cfg->i2c, BME280_REG_CALIB_H1, &data->calib.dig_H1, 1);
	if (rc < 0) {
		return rc;
	}

	data->calib.dig_T1 = (uint16_t)(tp[1] << 8 | tp[0]);
	data->calib.dig_T2 = (int16_t)(tp[3] << 8 | tp[2]);
	data->calib.dig_T3 = (int16_t)(tp[5] << 8 | tp[4]);
	data->calib.dig_P1 = (uint16_t)(tp[7] << 8 | tp[6]);
	data->calib.dig_P2 = (int16_t)(tp[9] << 8 | tp[8]);
	data->calib.dig_P3 = (int16_t)(tp[11] << 8 | tp[10]);
	data->calib.dig_P4 = (int16_t)(tp[13] << 8 | tp[12]);
	data->calib.dig_P5 = (int16_t)(tp[15] << 8 | tp[14]);
	data->calib.dig_P6 = (int16_t)(tp[17] << 8 | tp[16]);
	data->calib.dig_P7 = (int16_t)(tp[19] << 8 | tp[18]);
	data->calib.dig_P8 = (int16_t)(tp[21] << 8 | tp[20]);
	data->calib.dig_P9 = (int16_t)(tp[23] << 8 | tp[22]);

	data->calib.dig_H2 = (int16_t)(h[1] << 8 | h[0]);
	data->calib.dig_H3 = h[2];
	/* dig_H4 / dig_H5 are 12-bit signed, split across 0xE4/0xE5/0xE6. */
	data->calib.dig_H4 = (int16_t)(((int8_t)h[3] << 4) | (h[4] & 0x0F));
	data->calib.dig_H5 = (int16_t)(((int8_t)h[5] << 4) | (h[4] >> 4));
	data->calib.dig_H6 = (int8_t)h[6];

	return 0;
}

/* ------------------------------------------------------------------ */
/* Sensor subsystem API                                                */
/* ------------------------------------------------------------------ */

static int bme280_sample_fetch(const struct device *dev, enum sensor_channel chan)
{
	const struct bme280_config *cfg = dev->config;
	struct bme280_data *data = dev->data;
	uint8_t raw[8]; /* 0xF7..0xFE: press[3], temp[3], hum[2] */
	uint8_t status;
	int rc;

	if (chan != SENSOR_CHAN_ALL && chan != SENSOR_CHAN_AMBIENT_TEMP &&
	    chan != SENSOR_CHAN_HUMIDITY && chan != SENSOR_CHAN_PRESS) {
		return -ENOTSUP;
	}

	/* ctrl_hum must be written before ctrl_meas for the change to latch. */
	rc = bme280_reg_write(&cfg->i2c, BME280_REG_CTRL_HUM, BME280_OSRS_X1);
	if (rc < 0) {
		return rc;
	}

	/* Trigger one forced measurement: osrs_t=x1, osrs_p=x1, mode=forced. */
	uint8_t ctrl_meas = (BME280_OSRS_X1 << 5) | (BME280_OSRS_X1 << 2) |
			    BME280_MODE_FORCED;

	rc = bme280_reg_write(&cfg->i2c, BME280_REG_CTRL_MEAS, ctrl_meas);
	if (rc < 0) {
		return rc;
	}

	/* Wait for the conversion to complete (measuring bit clears). */
	bool measurement_done = false;

	for (int i = 0; i < BME280_MEAS_POLL_MAX; i++) {
		rc = bme280_burst_read(&cfg->i2c, BME280_REG_STATUS, &status, 1);
		if (rc < 0) {
			return rc;
		}
		if ((status & BME280_STATUS_MEASURING) == 0) {
			measurement_done = true;
			break;
		}
		k_msleep(BME280_MEAS_POLL_DELAY_MS);
	}

	if (!measurement_done) {
		LOG_WRN("measurement timed out after %d ms",
			BME280_MEAS_POLL_MAX * BME280_MEAS_POLL_DELAY_MS);
		return -ETIMEDOUT;
	}

	rc = bme280_burst_read(&cfg->i2c, BME280_REG_PRESS_MSB, raw, sizeof(raw));
	if (rc < 0) {
		return rc;
	}

	data->adc_P = ((uint32_t)raw[0] << 12) | ((uint32_t)raw[1] << 4) | (raw[2] >> 4);
	data->adc_T = ((uint32_t)raw[3] << 12) | ((uint32_t)raw[4] << 4) | (raw[5] >> 4);
	data->adc_H = ((uint32_t)raw[6] << 8) | raw[7];

	data->temp_c = bme280_compensate_temperature(data->adc_T, &data->calib,
						     &data->t_fine);
	data->press_kpa =
		bme280_compensate_pressure(data->adc_P, &data->calib, data->t_fine) /
		1000.0;
	data->hum_pct = bme280_compensate_humidity(data->adc_H, &data->calib, data->t_fine);

	/* Integer milli-units avoid requiring floating-point printf support. */
	LOG_DBG("fetch: T=%d mC P=%d Pa H=%d m%%RH",
		(int)(data->temp_c * 1000.0), (int)(data->press_kpa * 1000.0),
		(int)(data->hum_pct * 1000.0));
	return 0;
}

static int bme280_channel_get(const struct device *dev, enum sensor_channel chan,
			      struct sensor_value *val)
{
	struct bme280_data *data = dev->data;

	switch (chan) {
	case SENSOR_CHAN_AMBIENT_TEMP:
		sensor_value_from_double(val, data->temp_c);
		break;
	case SENSOR_CHAN_HUMIDITY:
		sensor_value_from_double(val, data->hum_pct);
		break;
	case SENSOR_CHAN_PRESS:
		sensor_value_from_double(val, data->press_kpa);
		break;
	default:
		return -ENOTSUP;
	}
	return 0;
}

static const struct sensor_driver_api bme280_driver_api = {
	.sample_fetch = bme280_sample_fetch,
	.channel_get = bme280_channel_get,
};

/* ------------------------------------------------------------------ */
/* Init + instantiation                                                */
/* ------------------------------------------------------------------ */

static int bme280_init(const struct device *dev)
{
	const struct bme280_config *cfg = dev->config;
	uint8_t chip_id = 0;
	int rc;

	if (!i2c_is_ready_dt(&cfg->i2c)) {
		LOG_ERR("I2C bus not ready");
		return -ENODEV;
	}

	rc = bme280_burst_read(&cfg->i2c, BME280_REG_CHIP_ID, &chip_id, 1);
	if (rc < 0) {
		LOG_ERR("chip id read failed (%d)", rc);
		return rc;
	}
	if (chip_id != BME280_CHIP_ID) {
		LOG_ERR("unexpected chip id 0x%02x (want 0x%02x)", chip_id,
			BME280_CHIP_ID);
		return -ENODEV;
	}

	rc = bme280_read_calib(dev);
	if (rc < 0) {
		LOG_ERR("calibration read failed (%d)", rc);
		return rc;
	}

	/* Start in sleep mode; sample_fetch triggers forced measurements. */
	rc = bme280_reg_write(&cfg->i2c, BME280_REG_CTRL_MEAS, BME280_MODE_SLEEP);
	if (rc < 0) {
		return rc;
	}

	LOG_INF("BME280 ready at I2C addr 0x%02x", cfg->i2c.addr);
	return 0;
}

#define BME280_INIT(inst)                                                        \
	static struct bme280_data bme280_data_##inst;                            \
	static const struct bme280_config bme280_config_##inst = {               \
		.i2c = I2C_DT_SPEC_INST_GET(inst),                               \
	};                                                                       \
	SENSOR_DEVICE_DT_INST_DEFINE(inst, bme280_init, NULL, &bme280_data_##inst,      \
			      &bme280_config_##inst, POST_KERNEL,                \
			      CONFIG_SENSOR_INIT_PRIORITY, &bme280_driver_api);

DT_INST_FOREACH_STATUS_OKAY(BME280_INIT)

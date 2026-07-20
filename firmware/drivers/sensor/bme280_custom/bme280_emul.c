/*
 * bme280_emul.c — BME280 I2C register-file emulator for native_sim.
 *
 * Backs the "vendor,bme280-custom" devicetree node on the native simulator via
 * Zephyr's I2C emulator framework (CONFIG_I2C_EMUL). It presents a full BME280
 * register map: chip ID (0xD0=0x60), calibration (0x88..0x9F, 0xA1, 0xE1..0xE7),
 * control (0xF2/0xF4/0xF5), status (0xF3), and data (0xF7..0xFE).
 *
 * The register file is pre-loaded with a self-consistent calibration + raw ADC
 * fixture that the Bosch compensation formulas convert to *known* physical
 * values, so the bme280 ztest can assert exact results from a full I2C
 * round-trip:
 *
 *     temperature = 25.000 C      (adc_T = 409600, dig_T2 = 5120)
 *     pressure    = 101.325 kPa   (adc_P = 947251, dig_P1 = 6250)
 *     humidity    = 50.000 %RH    (adc_H = 200,    dig_H2 = 16384)
 *
 * The fixture deliberately zeroes the cross-coupling coefficients so each
 * channel's expected value is hand-derivable (see docs and the bme280 ztest).
 */

#define DT_DRV_COMPAT vendor_bme280_custom

#include <errno.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/i2c_emul.h>
#include <zephyr/logging/log.h>

#include "bme280_custom.h"

LOG_MODULE_REGISTER(BME280_EMUL, CONFIG_SENSOR_LOG_LEVEL);

struct bme280_emul_cfg {
	uint16_t addr; /* 7-bit I2C address from devicetree `reg` */
};

struct bme280_emul_data {
	uint8_t reg[256]; /* full BME280 register file */
};

/* Populate the register file with chip ID + the known-answer fixture. */
static void bme280_emul_load_fixture(struct bme280_emul_data *d)
{
	memset(d->reg, 0, sizeof(d->reg));

	/* Chip ID. */
	d->reg[BME280_REG_CHIP_ID] = BME280_CHIP_ID;

	/*
	 * Calibration (little-endian per datasheet memory map).
	 * dig_T1=0, dig_T2=5120 (0x1400), dig_T3=0.
	 */
	d->reg[0x88] = 0x00; d->reg[0x89] = 0x00; /* dig_T1 = 0      */
	d->reg[0x8A] = 0x00; d->reg[0x8B] = 0x14; /* dig_T2 = 5120   */
	d->reg[0x8C] = 0x00; d->reg[0x8D] = 0x00; /* dig_T3 = 0      */
	/* dig_P1 = 6250 (0x186A); dig_P2..dig_P9 = 0. */
	d->reg[0x8E] = 0x6A; d->reg[0x8F] = 0x18;
	/* 0x90..0x9F stay zero (dig_P2..dig_P9). */

	/* dig_H1 (0xA1) = 0. */
	d->reg[0xA1] = 0x00;
	/* dig_H2 = 16384 (0x4000); dig_H3..dig_H6 = 0. */
	d->reg[0xE1] = 0x00; d->reg[0xE2] = 0x40;
	d->reg[0xE3] = 0x00; /* dig_H3 */
	d->reg[0xE4] = 0x00; /* dig_H4 high */
	d->reg[0xE5] = 0x00; /* dig_H4 low / dig_H5 low nibbles */
	d->reg[0xE6] = 0x00; /* dig_H5 high */
	d->reg[0xE7] = 0x00; /* dig_H6 */

	/*
	 * Raw ADC data registers.
	 * adc_P = 947251 (0xE7433): F7=0xE7, F8=0x43, F9=0x30
	 * adc_T = 409600 (0x64000): FA=0x64, FB=0x00, FC=0x00
	 * adc_H = 200    (0x00C8): FD=0x00, FE=0xC8
	 */
	d->reg[0xF7] = 0xE7; d->reg[0xF8] = 0x43; d->reg[0xF9] = 0x30;
	d->reg[0xFA] = 0x64; d->reg[0xFB] = 0x00; d->reg[0xFC] = 0x00;
	d->reg[0xFD] = 0x00; d->reg[0xFE] = 0xC8;

	/* Status: not measuring, no im_update -> driver poll proceeds at once. */
	d->reg[BME280_REG_STATUS] = 0x00;
	/* Default control: sleep mode, osrs x0. */
	d->reg[BME280_REG_CTRL_MEAS] = BME280_MODE_SLEEP;
	d->reg[BME280_REG_CTRL_HUM] = 0x00;
	d->reg[BME280_REG_CONFIG] = 0x00;
}

static uint8_t bme280_emul_reg_read(struct bme280_emul_data *d, uint8_t reg)
{
	return d->reg[reg];
}

static void bme280_emul_reg_write(struct bme280_emul_data *d, uint8_t reg, uint8_t val)
{
	d->reg[reg] = val;

	switch (reg) {
	case BME280_REG_RESET:
		if (val == BME280_RESET_MAGIC) {
			bme280_emul_load_fixture(d);
			LOG_DBG("emul: soft reset");
		}
		break;
	case BME280_REG_CTRL_MEAS:
		/*
		 * Forced/normal mode would start a conversion; the fixture data
		 * is already present, so we simply leave status "not measuring"
		 * and the driver reads the pre-loaded result immediately.
		 */
		LOG_DBG("emul: ctrl_meas=0x%02x", val);
		break;
	default:
		break;
	}
}

/*
 * I2C emulator transfer handler. Models a register-mapped I2C target:
 *   - a write message sets the register pointer (first byte) and optionally
 *     writes subsequent bytes to consecutive registers;
 *   - a following read message returns bytes starting at the pointer,
 *     auto-incrementing.
 */
static int bme280_emul_transfer(const struct emul *target, struct i2c_msg *msgs,
				int num_msgs, int addr)
{
	struct bme280_emul_data *d = target->data;
	uint8_t reg;

	ARG_UNUSED(addr);

	if (num_msgs < 1) {
		return -EIO;
	}
	/* First message must be a write that establishes the register pointer. */
	if (msgs[0].flags & I2C_MSG_READ) {
		LOG_ERR("emul: first message must be a write (register pointer)");
		return -EIO;
	}
	if (msgs[0].len < 1 || msgs[0].buf == NULL) {
		return -EIO;
	}

	reg = msgs[0].buf[0];
	/* Remaining bytes of the write message are register data. */
	for (uint16_t i = 1; i < msgs[0].len; i++) {
		bme280_emul_reg_write(d, (uint8_t)(reg + i - 1), msgs[0].buf[i]);
	}

	/* Optional second message: a read burst from the pointer. */
	if (num_msgs >= 2 && (msgs[1].flags & I2C_MSG_READ)) {
		if (msgs[1].buf == NULL) {
			return -EIO;
		}
		for (uint16_t i = 0; i < msgs[1].len; i++) {
			msgs[1].buf[i] = bme280_emul_reg_read(d, (uint8_t)(reg + i));
		}
	}

	return 0;
}

static const struct i2c_emul_api bme280_emul_api = {
	.transfer = bme280_emul_transfer,
};

static int bme280_emul_init(const struct emul *target, const struct device *parent)
{
	struct bme280_emul_data *d = target->data;
	const struct bme280_emul_cfg *cfg = target->cfg;

	ARG_UNUSED(parent);

	bme280_emul_load_fixture(d);
	LOG_INF("BME280 emulator ready at I2C addr 0x%02x", cfg->addr);
	return 0;
}

#define BME280_EMUL_INIT(inst)                                                   \
	static struct bme280_emul_data bme280_emul_data_##inst;                  \
	static const struct bme280_emul_cfg bme280_emul_cfg_##inst = {           \
		.addr = DT_INST_REG_ADDR(inst),                                  \
	};                                                                       \
	EMUL_DT_INST_DEFINE(inst, bme280_emul_init, &bme280_emul_data_##inst,    \
			    &bme280_emul_cfg_##inst, &bme280_emul_api, NULL)

DT_INST_FOREACH_STATUS_OKAY(BME280_EMUL_INIT)

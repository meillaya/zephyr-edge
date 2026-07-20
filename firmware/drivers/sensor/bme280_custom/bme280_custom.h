/*
 * bme280_custom.h — Custom Bosch BME280 I2C sensor driver (public interface).
 *
 * Exposes the BME280 register map, calibration layout, and the Bosch datasheet
 * floating-point compensation functions. The compensation functions are public
 * so the ztest suite can validate the math with known-answer vectors in
 * addition to the full I2C round-trip against the emulator.
 *
 * Reference: Bosch BST-BME280-DS002 (BME280 datasheet), §4.2 compensation.
 */
#ifndef ZEPHYR_EDGE_PLATFORM_BME280_CUSTOM_H_
#define ZEPHYR_EDGE_PLATFORM_BME280_CUSTOM_H_

#include <stdbool.h>
#include <stdint.h>

/* ---- Register map (BME280 datasheet §4.2 / Table 17-18) ---- */
#define BME280_REG_CALIB_TP   0x88 /* dig_T1..dig_P9: 0x88..0x9F (26 bytes) */
#define BME280_REG_CALIB_H1   0xA1 /* dig_H1 (1 byte)                       */
#define BME280_REG_CHIP_ID    0xD0 /* chip id, reads 0x60                   */
#define BME280_REG_RESET      0xE0 /* write 0xB6 to soft-reset              */
#define BME280_REG_CALIB_H2H6 0xE1 /* dig_H2..dig_H6: 0xE1..0xE7 (7 bytes)  */
#define BME280_REG_CTRL_HUM   0xF2 /* osrs_h[2:0]                           */
#define BME280_REG_STATUS     0xF3 /* measuring[3], im_update[0]            */
#define BME280_REG_CTRL_MEAS  0xF4 /* osrs_t[7:5], osrs_p[4:2], mode[1:0]   */
#define BME280_REG_CONFIG     0xF5 /* t_sb[7:5], filter[4:2], spi3w[0]      */
#define BME280_REG_PRESS_MSB  0xF7 /* press: 0xF7..0xF9 (20-bit)            */
#define BME280_REG_TEMP_MSB   0xFA /* temp:  0xFA..0xFC (20-bit)            */
#define BME280_REG_HUM_MSB    0xFD /* hum:   0xFD..0xFE (16-bit)            */

#define BME280_CHIP_ID        0x60
#define BME280_RESET_MAGIC    0xB6

/* ctrl_meas / ctrl_hum mode + oversampling fields */
#define BME280_MODE_SLEEP     0x00
#define BME280_MODE_FORCED    0x01
#define BME280_MODE_NORMAL    0x03
#define BME280_OSRS_X1        0x01 /* oversampling x1 encoding              */

/* status register bits */
#define BME280_STATUS_MEASURING BIT(3)
#define BME280_STATUS_IM_UPDATE BIT(0)

/* Calibration coefficients (BME280 datasheet §4.2.2). */
struct bme280_calib {
	uint16_t dig_T1;
	int16_t  dig_T2;
	int16_t  dig_T3;
	uint16_t dig_P1;
	int16_t  dig_P2;
	int16_t  dig_P3;
	int16_t  dig_P4;
	int16_t  dig_P5;
	int16_t  dig_P6;
	int16_t  dig_P7;
	int16_t  dig_P8;
	int16_t  dig_P9;
	uint8_t  dig_H1;
	int16_t  dig_H2;
	uint8_t  dig_H3;
	int16_t  dig_H4;
	int16_t  dig_H5;
	int8_t   dig_H6;
};

/*
 * Bosch floating-point compensation (datasheet §4.2.3).
 *
 * bme280_compensate_temperature() must be called first; it writes *t_fine,
 * which the pressure and humidity formulas consume.
 *
 * @return temperature in degrees Celsius.
 */
double bme280_compensate_temperature(int32_t adc_T, const struct bme280_calib *c,
				     double *t_fine);

/* @return pressure in pascals (Pa). Requires t_fine from temperature. */
double bme280_compensate_pressure(int32_t adc_P, const struct bme280_calib *c,
				  double t_fine);

/* @return relative humidity in %RH, clamped to [0, 100]. Requires t_fine. */
double bme280_compensate_humidity(int32_t adc_H, const struct bme280_calib *c,
				  double t_fine);

#endif /* ZEPHYR_EDGE_PLATFORM_BME280_CUSTOM_H_ */

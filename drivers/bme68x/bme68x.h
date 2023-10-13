/*
 * Copyright (c) 2018 Bosch Sensortec GmbH
 * Copyright (c) 2022, Leonard Pollak
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __ZEPHYR_DRIVERS_SENSOR_BME68X_H__
#define __ZEPHYR_DRIVERS_SENSOR_BME68X_H__

#include <zephyr/types.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/i2c.h>

#define DT_DRV_COMPAT bosch_bme68x

#define BME68X_BUS_I2C DT_ANY_INST_ON_BUS_STATUS_OKAY(i2c)

union bme68x_bus {
	struct i2c_dt_spec i2c;
};

typedef int (*bme68x_bus_check_fn)(const union bme68x_bus *bus);
typedef int (*bme68x_reg_read_fn)(const struct device *dev, uint8_t start, uint8_t *buf, int size);
typedef int (*bme68x_reg_write_fn)(const struct device *dev, uint8_t reg, uint8_t val);

struct bme68x_bus_io {
	bme68x_bus_check_fn check;
	bme68x_reg_read_fn read;
	bme68x_reg_write_fn write;
};

#if BME68X_BUS_I2C
extern const struct bme68x_bus_io bme68x_bus_io_i2c;
#endif

struct bme68x_config {
	union bme68x_bus bus;
	const struct bme68x_bus_io *bus_io;
};

#define BME68X_CHIP_ID 0x61

#define BME68X_LEN_FIELD     15
#define BME68X_LEN_COEFF_ALL 42
#define BME68X_LEN_COEFF1    23
#define BME68X_LEN_COEFF2    14
#define BME68X_LEN_COEFF3    5

#define BME68X_REG_COEFF3	 0x00
#define BME68X_REG_FIELD0	 0x1d
#define BME68X_REG_IDAC_HEAT0	 0x50
#define BME68X_REG_RES_HEAT0	 0x5A
#define BME68X_REG_GAS_WAIT0	 0x64
#define BME68X_REG_SHD_HEATR_DUR 0x6E
#define BME68X_REG_CTRL_GAS_0	 0x70
#define BME68X_REG_CTRL_GAS_1	 0x71
#define BME68X_REG_CTRL_HUM	 0x72
#define BME68X_REG_CTRL_MEAS	 0x74
#define BME68X_REG_CONFIG	 0x75
#define BME68X_REG_STATUS	 0x73
#define BME68X_REG_UNIQUE_ID	 0x83
#define BME68X_REG_COEFF1	 0x8a
#define BME68X_REG_COEFF2	 0xe1
#define BME68X_REG_CHIP_ID	 0xd0
#define BME68X_REG_SOFT_RESET	 0xe0

#define BME68X_MSK_NEW_DATA	0x80
#define BME68X_MSK_GAS_RANGE	0x0f
#define BME68X_MSK_RH_RANGE	0x30
#define BME68X_MSK_RANGE_SW_ERR 0xf0
#define BME68X_MSK_HEATR_STAB	0x10

#if defined CONFIG_BME68X_TEMP_OVER_1X
#define BME68X_TEMP_OVER (1 << 5)
#elif defined CONFIG_BME68X_TEMP_OVER_2X
#define BME68X_TEMP_OVER (2 << 5)
#elif defined CONFIG_BME68X_TEMP_OVER_4X
#define BME68X_TEMP_OVER (3 << 5)
#elif defined CONFIG_BME68X_TEMP_OVER_8X
#define BME68X_TEMP_OVER (4 << 5)
#elif defined CONFIG_BME68X_TEMP_OVER_16X
#define BME68X_TEMP_OVER (5 << 5)
#endif

#if defined CONFIG_BME68X_PRESS_OVER_1X
#define BME68X_PRESS_OVER (1 << 2)
#elif defined CONFIG_BME68X_PRESS_OVER_2X
#define BME68X_PRESS_OVER (2 << 2)
#elif defined CONFIG_BME68X_PRESS_OVER_4X
#define BME68X_PRESS_OVER (3 << 2)
#elif defined CONFIG_BME68X_PRESS_OVER_8X
#define BME68X_PRESS_OVER (4 << 2)
#elif defined CONFIG_BME68X_PRESS_OVER_16X
#define BME68X_PRESS_OVER (5 << 2)
#endif

#if defined CONFIG_BME68X_HUMIDITY_OVER_1X
#define BME68X_HUMIDITY_OVER 1
#elif defined CONFIG_BME68X_HUMIDITY_OVER_2X
#define BME68X_HUMIDITY_OVER 2
#elif defined CONFIG_BME68X_HUMIDITY_OVER_4X
#define BME68X_HUMIDITY_OVER 3
#elif defined CONFIG_BME68X_HUMIDITY_OVER_8X
#define BME68X_HUMIDITY_OVER 4
#elif defined CONFIG_BME68X_HUMIDITY_OVER_16X
#define BME68X_HUMIDITY_OVER 5
#endif

#if defined CONFIG_BME68X_HEATR_TEMP_LP
#define BME68X_HEATR_TEMP 320
#elif defined CONFIG_BME68X_HEATR_TEMP_ULP
#define BME68X_HEATR_TEMP 400
#endif

#if defined CONFIG_BME68X_HEATR_DUR_LP
#define BME68X_HEATR_DUR_MS 197
#elif defined CONFIG_BME68X_HEATR_DUR_ULP
#define BME68X_HEATR_DUR_MS 1943
#endif

#if defined CONFIG_BME68X_FILTER_OFF
#define BME68X_FILTER 0
#elif defined CONFIG_BME68X_FILTER_2
#define BME68X_FILTER (1 << 2)
#elif defined CONFIG_BME68X_FILTER_4
#define BME68X_FILTER (2 << 2)
#elif defined CONFIG_BME68X_FILTER_8
#define BME68X_FILTER (3 << 2)
#elif defined CONFIG_BME68X_FILTER_16
#define BME68X_FILTER (4 << 2)
#elif defined CONFIG_BME68X_FILTER_32
#define BME68X_FILTER (5 << 2)
#elif defined CONFIG_BME68X_FILTER_64
#define BME68X_FILTER (6 << 2)
#elif defined CONFIG_BME68X_FILTER_128
#define BME68X_FILTER (7 << 2)
#endif

#define BME68X_MODE_SLEEP  0
#define BME68X_MODE_FORCED 1

#define BME68X_CTRL_MEAS_VAL  (BME68X_PRESS_OVER | BME68X_TEMP_OVER | BME68X_MODE_FORCED)
#define BME68X_CONFIG_VAL     BME68X_FILTER
#define BME68X_CTRL_GAS_1_VAL 0x10
#define BME68X_CTRL_GAS_1_OFF 0x00

#define BME68X_CONCAT_BYTES(msb, lsb) (((uint16_t)msb << 8) | (uint16_t)lsb)

struct bme68x_data {
	/* Compensation parameters. */
	uint16_t par_h1;
	uint16_t par_h2;
	int8_t par_h3;
	int8_t par_h4;
	int8_t par_h5;
	uint8_t par_h6;
	int8_t par_h7;
	int8_t par_gh1;
	int16_t par_gh2;
	int8_t par_gh3;
	uint16_t par_t1;
	int16_t par_t2;
	int8_t par_t3;
	uint16_t par_p1;
	int16_t par_p2;
	int8_t par_p3;
	int16_t par_p4;
	int16_t par_p5;
	int8_t par_p6;
	int8_t par_p7;
	int16_t par_p8;
	int16_t par_p9;
	uint8_t par_p10;
	uint8_t res_heat_range;
	int8_t res_heat_val;
	int8_t range_sw_err;

	/* Calculated sensor values. */
	int32_t calc_temp;
	uint32_t calc_press;
	uint32_t calc_humidity;
	uint32_t calc_gas_resistance;

	/* Additional information */
	uint8_t new_data;
	uint8_t heatr_stab;

	/* Carryover between temperature and pressure/humidity compensation. */
	int32_t t_fine;

	uint8_t chip_id;
};

#endif /* __ZEPHYR_DRIVERS_SENSOR_BME68X_H__ */
